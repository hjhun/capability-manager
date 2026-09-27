// SPDX-License-Identifier: Apache-2.0
#include "amd-module/action_import.hh"
#include <set>
#include <mutex>
namespace capmgr {
namespace {
void Collect(const Json& node,std::set<std::string>& refs,std::set<std::string>& boxes,
             unsigned depth=0) {
  if(depth>128)throw Error(ErrorCode::kLimit,"Entity schema nesting limit");
  if(node.is_object()) {
    if(node.contains("type") && node["type"].is_string()) {
      std::string name=node["type"];
      if(name.find('.')!=std::string::npos)refs.insert(name);
    }
    if(node.contains("base") && node["base"].is_string()) {
      std::string name=node["base"];
      if(!name.empty()){refs.insert(name);boxes.insert(name);}
    }
    for(auto it=node.begin();it!=node.end();++it) {
      // Schema annotations/examples are data, not schemas containing entity refs.
      if(it.key()=="properties" || it.key()=="$defs" || it.key()=="definitions" ||
         it.key()=="patternProperties") {
        if(it.value().is_object())
          for(const auto& schema:it.value())Collect(schema,refs,boxes,depth+1);
      } else if(it.key()=="items" || it.key()=="additionalProperties" ||
                it.key()=="allOf" || it.key()=="anyOf" || it.key()=="oneOf" ||
                it.key()=="not" || it.key()=="if" || it.key()=="then" || it.key()=="else")
        Collect(it.value(),refs,boxes,depth+1);
    }
  } else if(node.is_array())for(const auto& schema:node)Collect(schema,refs,boxes,depth+1);
}
bool Derived(const std::string& name,const std::string& base,const std::map<std::string,Json>& entities) {
  std::set<std::string> visited;std::string current=name;
  while(visited.insert(current).second) {
    if(current==base)return true;
    auto it=entities.find(current);if(it==entities.end())return false;
    if(!it->second.contains("base"))return false;
    if(!it->second["base"].is_string())throw Error(ErrorCode::kInvalid,"Invalid Entity base");
    current=it->second["base"].get<std::string>();if(current.empty())return false;
  }
  return false;
}
}
Json EntityClosure(const Json& action,const std::map<std::string,Json>& entities) {
  std::set<std::string> refs,boxes,processed;
  for(const char* field:{"inputSchema","outputSchema","eventSchema"})
    if(action.contains(field))Collect(action[field],refs,boxes);
  Json out=Json::object();
  for(;;) {
    size_t previous=refs.size();
    size_t previous_boxes=boxes.size();
    for(const auto& [name,entity]:entities)
      for(const auto& box:boxes)if(Derived(name,box,entities))refs.insert(name);
    for(const auto& name:std::set<std::string>(refs)) {
      if(!processed.insert(name).second)continue;
      auto it=entities.find(name);
      if(it==entities.end())throw Error(ErrorCode::kInvalid,"Missing Entity: "+name);
      const auto& entity=it->second;
      if(!entity.is_object() || entity.value("typeName",Json())!=name ||
         !entity.contains("dataSchema") || !entity["dataSchema"].is_object())
        throw Error(ErrorCode::kInvalid,"Invalid Entity: "+name);
      out[name]=entity;
      if(entity.contains("base")) {
        if(!entity["base"].is_string())throw Error(ErrorCode::kInvalid,"Invalid Entity base");
        auto base=entity["base"].get<std::string>();if(!base.empty())refs.insert(base);
      }
      Collect(entity["dataSchema"],refs,boxes);
      if(refs.size()>4096)throw Error(ErrorCode::kLimit,"Entity closure limit");
    }
    if(previous==refs.size() && previous_boxes==boxes.size() && processed.size()==refs.size())break;
  }
  return out;
}
std::vector<Entry> ReadActionSnapshot(const std::string& path) {
  Database source(path,Database::Access::kReadOnly);
  Transaction snapshot(source,false);
  std::map<std::string,Json> entities;
  { Statement q(source.handle(),"SELECT entity_name,json_str FROM entity");
    while(q.Step()) {
      auto parsed=Json::parse(q.Text(1),nullptr,false);
      if(parsed.is_discarded())throw Error(ErrorCode::kInvalid,"Malformed source Entity");
      entities.emplace(q.Text(0),std::move(parsed));
    }
  }
  std::vector<Entry> entries;std::set<std::string> ids;
  Statement actions(source.handle(),"SELECT action_name,json_str FROM action ORDER BY action_name");
  while(actions.Step()) {
    Json action=Json::parse(actions.Text(1),nullptr,false);
    if(action.is_discarded() || !action.is_object())throw Error(ErrorCode::kInvalid,"Malformed Action");
    Entry e;e.kind=Kind::kAction;e.owner="@action-source";e.key=actions.Text(0);
    e.id=CanonicalId(e.kind,e.key);e.name=e.key;
    if(!ids.insert(e.id).second)throw Error(ErrorCode::kConflict,"Duplicate source Action");
    if(action.value("name",Json())!=e.key || !action.contains("description") ||
       !action["description"].is_string())throw Error(ErrorCode::kInvalid,"Action identity mismatch");
    e.desc=action["description"].get<std::string>();e.detail=Json::object();
    for(const char* field:{"inputSchema","outputSchema","eventSchema"}) {
      if(action.contains(field)) {
        if(!action[field].is_object())throw Error(ErrorCode::kInvalid,"Invalid Action schema");
        e.detail[field]=action[field];
      } else if(std::string(field)=="inputSchema")throw Error(ErrorCode::kInvalid,"Missing Action input schema");
    }
    e.detail["entities"]=EntityClosure(action,entities);
    Json confirmation=action.value("requiresConfirmation",Json(false));
    e.detail["requiresConfirmation"]=confirmation==true || confirmation=="true";
    e.detail["providerAppIds"]=Json::array();
    Statement providers(source.handle(),"SELECT DISTINCT appid FROM action_provider WHERE action_name=? ORDER BY appid");
    providers.Bind(1,e.key);while(providers.Step())e.detail["providerAppIds"].push_back(providers.Text(0));
    e.detail["defaultProviderAppId"]=nullptr;
    if(action.value("type",Json())=="tidl" && action.contains("details") && action["details"].is_object()) {
      const auto& details=action["details"];
      if(details.contains("appid") && details["appid"].is_string() && details["appid"]!="")
        e.detail["defaultProviderAppId"]=details["appid"];
    }
    entries.push_back(std::move(e));
  }
  snapshot.Commit();return entries;
}
}
namespace capmgr {
bool SynchronizeActions(Catalog& catalog,const std::string& source_path,
                        const std::function<void(uint64_t)>& changed) {
  // One importer per process serializes source snapshots through catalog commit.
  // Deployment must enforce a single AMD importer across processes.
  static std::mutex import_mutex;
  uint64_t revision;bool published;
  {
    std::lock_guard guard(import_mutex);
    auto entries=ReadActionSnapshot(source_path);
    published=catalog.PublishActions(entries);
    revision=catalog.Revision();
  }
  // No source/capability transaction or importer lock is held during notification.
  // Reconciliation replays the committed revision even after a previous delivery
  // failure or process restart. Receivers suppress revisions already observed.
  if(changed && revision)changed(revision);
  return published;
}
}
