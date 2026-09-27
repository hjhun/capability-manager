// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <memory>
#include <string>
#include "api/capmgr.h"
namespace capmgr {
// Private injection seam. Never installed or exported in libcapmgr.
class AccessGate {
 public:
  virtual ~AccessGate() = default;
  virtual std::string AuthorizeAndGetDatabase() = 0;
};
int CreateClient(AccessGate& gate, capmgr_client_h* client) noexcept;
}

#include "api/dispatcher.hh"
#include "launcher/runner.hh"
namespace capmgr {
class ExecutionBackend {
 public:
  virtual ~ExecutionBackend()=default;
  // Admission authorization/registered binding happens synchronously; failures
  // must not launch work. Worker must honor cancellation and a bounded deadline.
  virtual bool SupportsCancel(const Entry&) const {return false;}
  virtual void Admit(const Entry& entry,const Request& request)=0;
  virtual std::string Execute(const Entry& entry,const Request& request,
      const std::atomic<bool>& cancelled,const Dispatcher::Emit& event) {
    (void)entry;(void)request;(void)cancelled;(void)event;
    throw Error(ErrorCode::kUnsupported,"Execution backend is unavailable");
  }
  // Subscription acknowledgements are nonterminal results; closed events are
  // terminal events. Return only after native callbacks are quiescent. The
  // transport still owns cancellation acknowledgement and a bounded deadline.
  virtual void Run(const Entry& entry,const Request& request,const std::atomic<bool>& cancelled,
                   const Dispatcher::EmitFrame& emit) {
    auto result=Execute(entry,request,cancelled,[&](std::string json){emit({std::move(json),true,false});});
    emit({std::move(result),false,true});
  }
};
int CreateClient(AccessGate& gate,std::shared_ptr<ExecutionBackend> backend,
                 capmgr_client_h* client) noexcept;
void NotifyChanged(capmgr_client_h client,uint64_t revision);
}
