// SPDX-License-Identifier: Apache-2.0
#include "catalog/catalog.hh"
#include <cctype>
#include <set>
#include <sstream>
namespace capmgr {
namespace {
constexpr int kSchemaVersion = 2;
std::string Encode(const std::string& value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
      out += c;
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}
void CheckFilter(Kind kind) {
  if (kind < Kind::kAll || kind > Kind::kAction)
    throw Error(ErrorCode::kInvalid, "Invalid kind filter");
}
Json Summary(const Entry& e) {
  if (e.kind < Kind::kSkill || e.kind > Kind::kAction || e.id.empty() ||
      e.name.empty())
    throw Error(ErrorCode::kDatabase, "Corrupt catalog identity");
  return {{"id", e.id},
          {"name", e.name},
          {"desc", e.desc},
          {"kind", KindName(e.kind)}};
}
Json Serialize(const Entry& e) {
  return {{"id", e.id},
          {"name", e.name},
          {"desc", e.desc},
          {"keywords", e.keywords},
          {"kind", static_cast<int>(e.kind)},
          {"owner", e.owner},
          {"app", e.app_id},
          {"detail", e.detail},
          {"resource", e.resource},
          {"executable", e.executable},
          {"key", e.key}};
}
Json ParseStored(const std::string& text, bool array = false) {
  Json value = Json::parse(text, nullptr, false);
  if (value.is_discarded() || (array ? !value.is_array() : !value.is_object()))
    throw Error(ErrorCode::kDatabase, "Corrupt stored catalog JSON");
  return value;
}
Entry Deserialize(const Json& j) {
  try {
    return {j.at("id"),
            j.at("name"),
            j.at("desc"),
            j.at("keywords"),
            static_cast<Kind>(j.at("kind").get<int>()),
            j.at("owner"),
            j.at("app"),
            j.at("detail"),
            j.at("resource"),
            j.at("executable"),
            j.at("key")};
  } catch (const Json::exception&) {
    throw Error(ErrorCode::kDatabase, "Corrupt pending catalog payload");
  }
}
}
const char* KindName(Kind kind) {
  switch (kind) {
    case Kind::kSkill:
      return "skill";
    case Kind::kAppSkill:
      return "app-skill";
    case Kind::kCli:
      return "cli";
    case Kind::kAction:
      return "action";
    default:
      throw Error(ErrorCode::kInvalid, "Invalid kind");
  }
}
std::string CanonicalId(Kind kind, const std::string& name,
                        const std::string& app_id) {
  try {
    (void)Json(name).dump();
    (void)Json(app_id).dump();
  } catch (const Json::exception&) {
    throw Error(ErrorCode::kInvalid, "Identity must be valid UTF-8");
  }
  if (name.empty() || name.size() > 512 ||
      name.find('\0') != std::string::npos ||
      (kind == Kind::kAppSkill && (app_id.empty() || app_id.size() > 512 ||
                                   app_id.find('\0') != std::string::npos)))
    throw Error(ErrorCode::kInvalid, "Invalid capability identity");
  return std::string(KindName(kind)) + ":" +
         (kind == Kind::kAppSkill ? Encode(app_id) + ":" : "") + Encode(name);
}
Catalog::Catalog(const std::string& path, Database::Access access)
    : db_(path, access), writer_(access == Database::Access::kWriter) {
  if (writer_) Migrate();
  ValidateVersion();
}
void Catalog::ValidateVersion() {
  Statement q(db_.handle(), "PRAGMA user_version");
  if (!q.Step() || q.Integer(0) != kSchemaVersion)
    throw Error(ErrorCode::kUnsupported, "Unsupported catalog schema");
  db_.Revision();
}
void Catalog::Migrate() {
  Transaction tx(db_);
  Statement version(db_.handle(), "PRAGMA user_version");
  version.Step();
  if (version.Integer(0) == 0) {
    Statement existing(
        db_.handle(),
        "SELECT count(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'");
    existing.Step();
    if (existing.Integer(0) != 0)
      throw Error(ErrorCode::kUnsupported,
                  "Refuse to adopt an unversioned database");
    db_.Exec(R"sql(
      CREATE TABLE catalog_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1),
        revision INTEGER NOT NULL CHECK(typeof(revision)='integer' AND revision>=0));
      INSERT INTO catalog_state VALUES(1,0);
      CREATE TABLE capability(id TEXT PRIMARY KEY, name TEXT NOT NULL,
        description TEXT NOT NULL, keywords TEXT NOT NULL, kind INTEGER NOT NULL,
        owner TEXT NOT NULL, app_id TEXT NOT NULL, detail TEXT NOT NULL,
        resource TEXT NOT NULL, executable TEXT NOT NULL, stable_key TEXT NOT NULL);
      CREATE INDEX capability_owner ON capability(owner);
      CREATE VIRTUAL TABLE capability_fts USING fts5(id UNINDEXED, name, keywords,
        description, tokenize='porter unicode61');
      CREATE TRIGGER capability_insert AFTER INSERT ON capability BEGIN
        INSERT INTO capability_fts(rowid,id,name,keywords,description)
          VALUES(new.rowid,new.id,new.name,new.keywords,new.description); END;
      CREATE TRIGGER capability_delete AFTER DELETE ON capability BEGIN
        DELETE FROM capability_fts WHERE rowid=old.rowid; END;
      CREATE TABLE pending(operation TEXT PRIMARY KEY, owner TEXT UNIQUE NOT NULL,
        payload TEXT NOT NULL);
      CREATE TABLE completed(operation TEXT PRIMARY KEY, success INTEGER NOT NULL);
      PRAGMA user_version=2;
    )sql");
  } else if (version.Integer(0) == 1) {
    db_.Exec(
        "CREATE TABLE IF NOT EXISTS completed(operation TEXT PRIMARY KEY, "
        "success INTEGER NOT NULL); PRAGMA user_version=2;");
  } else if (version.Integer(0) != kSchemaVersion) {
    throw Error(ErrorCode::kUnsupported, "Unsupported writer schema");
  }
  tx.Commit();
}
void Catalog::Validate(const std::string& owner,
                       const std::vector<Entry>& entries) {
  if (!writer_) throw Error(ErrorCode::kPermission, "Read-only catalog");
  if (owner.empty()) throw Error(ErrorCode::kInvalid, "Missing owner");
  std::set<std::string> ids;
  for (const auto& e : entries) {
    if (e.owner != owner || e.id != CanonicalId(e.kind, e.key, e.app_id) ||
        !e.detail.is_object() || !ids.insert(e.id).second)
      throw Error(ErrorCode::kInvalid, "Invalid or duplicate catalog entry");
    Statement existing(db_.handle(), "SELECT owner FROM capability WHERE id=?");
    existing.Bind(1, e.id);
    if (existing.Step() && existing.Text(0) != owner)
      throw Error(ErrorCode::kConflict, "Capability owned by another package");
    Statement pending(db_.handle(),
                      "SELECT payload FROM pending WHERE owner<>?");
    pending.Bind(1, owner);
    while (pending.Step()) {
      for (const auto& staged : ParseStored(pending.Text(0), true))
        if (Deserialize(staged).id == e.id)
          throw Error(ErrorCode::kConflict,
                      "Capability reserved by pending package");
    }
  }
}
void Catalog::Replace(const std::string& owner,
                      const std::vector<Entry>& entries) {
  Validate(owner, entries);
  Statement remove(db_.handle(), "DELETE FROM capability WHERE owner=?");
  remove.Bind(1, owner);
  remove.Step();
  for (const auto& e : entries) {
    Statement add(db_.handle(),
                  "INSERT INTO capability VALUES(?,?,?,?,?,?,?,?,?,?,?)");
    add.Bind(1, e.id);
    add.Bind(2, e.name);
    add.Bind(3, e.desc);
    add.Bind(4, e.keywords);
    add.Bind(5, static_cast<int64_t>(e.kind));
    add.Bind(6, e.owner);
    add.Bind(7, e.app_id);
    add.Bind(8, e.detail.dump());
    add.Bind(9, e.resource);
    add.Bind(10, e.executable);
    add.Bind(11, e.key);
    add.Step();
  }
  db_.Revision();  // Reject malformed stored revision before arithmetic.
  db_.Exec(
      "UPDATE catalog_state SET revision=revision+1 "
      "WHERE singleton=1 AND revision<9223372036854775807");
  if (sqlite3_changes(db_.handle()) != 1)
    throw Error(ErrorCode::kLimit, "Catalog revision exhausted");
}
bool Catalog::PublishActions(const std::vector<Entry>& entries) {
  Transaction tx(db_);
  for (const auto& entry : entries)
    if (entry.kind != Kind::kAction || entry.owner != "@action-source")
      throw Error(ErrorCode::kPermission,
                  "Only trusted Action snapshots may publish directly");
  Validate("@action-source", entries);
  Json expected = Json::object(), current = Json::object();
  for (const auto& entry : entries) expected[entry.id] = Serialize(entry);
  Foreach(Kind::kAction, [&](const Json& summary) {
    auto entry = GetPrivate(summary.at("id"));
    if (entry.owner != "@action-source")
      throw Error(ErrorCode::kConflict, "Foreign Action owner");
    current[entry.id] = Serialize(entry);
    return true;
  });
  if (current == expected) {
    tx.Commit();
    return false;
  }
  Replace("@action-source", entries);
  tx.Commit();
  return true;
}
void Catalog::Stage(const std::string& operation, const std::string& owner,
                    const std::vector<Entry>& entries) {
  if (operation.empty())
    throw Error(ErrorCode::kInvalid, "Missing operation ID");
  Transaction tx(db_);
  Statement done(db_.handle(), "SELECT 1 FROM completed WHERE operation=?");
  done.Bind(1, operation);
  if (done.Step())
    throw Error(ErrorCode::kConflict, "Completed operation cannot be restaged");
  Validate(owner, entries);
  Json payload = Json::array();
  for (const auto& e : entries) payload.push_back(Serialize(e));
  Statement old(db_.handle(),
                "SELECT owner,payload FROM pending WHERE operation=?");
  old.Bind(1, operation);
  if (old.Step()) {
    if (old.Text(0) != owner || old.Text(1) != payload.dump())
      throw Error(ErrorCode::kConflict, "Operation replay differs");
    tx.Commit();
    return;
  }
  Statement busy(db_.handle(), "SELECT 1 FROM pending WHERE owner=?");
  busy.Bind(1, owner);
  if (busy.Step())
    throw Error(ErrorCode::kBusy, "Package operation already pending");
  Statement add(db_.handle(), "INSERT INTO pending VALUES(?,?,?)");
  add.Bind(1, operation);
  add.Bind(2, owner);
  add.Bind(3, payload.dump());
  add.Step();
  tx.Commit();
}
void Catalog::Finalize(const std::string& operation, bool success) {
  Transaction tx(db_);
  Statement done(db_.handle(),
                 "SELECT success FROM completed WHERE operation=?");
  done.Bind(1, operation);
  if (done.Step()) {
    if (done.Type(0) != SQLITE_INTEGER ||
        (done.Integer(0) != 0 && done.Integer(0) != 1))
      throw Error(ErrorCode::kDatabase, "Corrupt final outcome");
    if (done.Integer(0) != static_cast<int64_t>(success))
      throw Error(ErrorCode::kConflict, "Final outcome replay differs");
    tx.Commit();
    return;
  }
  Statement read(db_.handle(),
                 "SELECT owner,payload FROM pending WHERE operation=?");
  read.Bind(1, operation);
  if (!read.Step())
    throw Error(ErrorCode::kNotFound, "Unknown pending operation");
  std::string owner = read.Text(0);
  std::vector<Entry> entries;
  for (const auto& e : ParseStored(read.Text(1), true))
    entries.push_back(Deserialize(e));
  if (success) Replace(owner, entries);
  Statement remove(db_.handle(), "DELETE FROM pending WHERE operation=?");
  remove.Bind(1, operation);
  remove.Step();
  Statement complete(db_.handle(), "INSERT INTO completed VALUES(?,?)");
  complete.Bind(1, operation);
  complete.Bind(2, static_cast<int64_t>(success));
  complete.Step();
  tx.Commit();
}
void Catalog::Foreach(Kind filter,
                      const std::function<bool(const Json&)>& callback) {
  CheckFilter(filter);
  Statement q(db_.handle(),
              "SELECT id,name,description,kind FROM capability "
              "WHERE (?=0 OR kind=?) ORDER BY id");
  q.Bind(1, static_cast<int64_t>(filter));
  q.Bind(2, static_cast<int64_t>(filter));
  while (q.Step()) {
    if (q.Type(0) != SQLITE_TEXT || q.Type(1) != SQLITE_TEXT ||
        q.Type(2) != SQLITE_TEXT || q.Type(3) != SQLITE_INTEGER)
      throw Error(ErrorCode::kDatabase, "Corrupt catalog summary");
    Entry e;
    e.id = q.Text(0);
    e.name = q.Text(1);
    e.desc = q.Text(2);
    e.kind = static_cast<Kind>(q.Integer(3));
    if (!callback(Summary(e))) break;
  }
}
Entry Catalog::GetPrivate(const std::string& id) {
  Statement q(
      db_.handle(),
      "SELECT id,name,description,keywords,kind,owner,app_id,"
      "detail,resource,executable,stable_key FROM capability WHERE id=?");
  q.Bind(1, id);
  if (!q.Step()) throw Error(ErrorCode::kNotFound, "Unknown capability");
  for (int column = 0; column < 11; ++column)
    if (q.Type(column) != (column == 4 ? SQLITE_INTEGER : SQLITE_TEXT))
      throw Error(ErrorCode::kDatabase, "Corrupt catalog field type");
  if (q.Integer(4) < 1 || q.Integer(4) > 4)
    throw Error(ErrorCode::kDatabase, "Corrupt catalog kind");
  return {q.Text(0),
          q.Text(1),
          q.Text(2),
          q.Text(3),
          static_cast<Kind>(q.Integer(4)),
          q.Text(5),
          q.Text(6),
          ParseStored(q.Text(7)),
          q.Text(8),
          q.Text(9),
          q.Text(10)};
}
Json Catalog::Get(const std::string& id) {
  Entry e = GetPrivate(id);
  Json result = Summary(e);
  for (const char* field :
       {"inputSchema", "outputSchema", "eventSchema", "entities",
        "requiresConfirmation", "providerAppIds", "defaultProviderAppId"})
    if (e.detail.contains(field)) result[field] = e.detail[field];
  if (e.kind == Kind::kSkill || e.kind == Kind::kAppSkill) {
    result["path"] = nullptr;
    result["available"] = false;
  }
  return result;
}
std::vector<Json> Catalog::Search(const std::string& query, Kind filter) {
  CheckFilter(filter);
  if (query.size() > 4096)
    throw Error(ErrorCode::kLimit, "Search query too large");
  std::vector<Json> results;
  if (query.empty()) return results;
  Transaction snapshot(db_, false);
  std::set<std::string> seen;
  auto collect = [&](Statement& q) {
    while (results.size() < 5 && q.Step()) {
      if (q.Type(0) != SQLITE_TEXT || q.Type(1) != SQLITE_TEXT ||
          q.Type(2) != SQLITE_TEXT || q.Type(3) != SQLITE_INTEGER)
        throw Error(ErrorCode::kDatabase, "Corrupt search row");
      if (!seen.insert(q.Text(0)).second) continue;
      Entry e;
      e.id = q.Text(0);
      e.name = q.Text(1);
      e.desc = q.Text(2);
      e.kind = static_cast<Kind>(q.Integer(3));
      results.push_back(Summary(e));
    }
  };
  {
    Statement exact(db_.handle(),
                    "SELECT id,name,description,kind FROM capability "
                    "WHERE (id=? OR name=? COLLATE NOCASE) AND (?=0 OR kind=?) "
                    "ORDER BY (id=?) DESC,id LIMIT 5");
    exact.Bind(1, query);
    exact.Bind(2, query);
    exact.Bind(3, static_cast<int64_t>(filter));
    exact.Bind(4, static_cast<int64_t>(filter));
    exact.Bind(5, query);
    collect(exact);
  }
  std::istringstream words(query);
  std::string word, match;
  while (words >> word) {
    if (!match.empty()) match += " AND ";
    match += '"';
    for (char c : word) {
      if (c == '"') match += '"';
      match += c;
    }
    match += '"';
  }
  if (!match.empty() && results.size() < 5) {
    Statement ranked(
        db_.handle(),
        "SELECT c.id,c.name,c.description,c.kind "
        "FROM capability_fts JOIN capability c ON c.rowid=capability_fts.rowid "
        "WHERE capability_fts MATCH ? AND (?=0 OR c.kind=?) "
        "ORDER BY bm25(capability_fts,0,10,5,1),c.id LIMIT 10");
    ranked.Bind(1, match);
    ranked.Bind(2, static_cast<int64_t>(filter));
    ranked.Bind(3, static_cast<int64_t>(filter));
    collect(ranked);
  }
  snapshot.Commit();
  return results;
}
}
