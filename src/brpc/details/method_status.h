// Copyright (c) 2015 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef  BRPC_METHOD_STATUS_H
#define  BRPC_METHOD_STATUS_H

#include "butil/macros.h"                  // DISALLOW_COPY_AND_ASSIGN
#include "bvar/bvar.h"                    // vars
#include "brpc/describable.h"
#include "brpc/concurrency_limiter.h"


namespace brpc {

class Controller;
class Server;
// Record accessing stats of a method.
class MethodStatus : public Describable {
public:
    MethodStatus();
    ~MethodStatus();

    // Call this function when the method is about to be called.
    // Returns false when the request reaches max_concurrency to the method
    // and is suggested to be rejected.
    bool OnRequested();

    // Call this when the method just finished.
    // `error_code' : The error code obtained from the controller. Equal to 
    // 0 when the call is successful.
    // `latency_us' : microseconds taken by a successful call. Latency can
    // be measured in this utility class as well, but the callsite often
    // did the time keeping and the cost is better saved. 
    void OnResponded(int error_code, int64_t latency_us);

    // Expose internal vars.
    // Return 0 on success, -1 otherwise.
    int Expose(const butil::StringPiece& prefix);

    // Describe internal vars, used by /status
    void Describe(std::ostream &os, const DescribeOptions&) const;

    // Current maximum concurrency of method.
    // Return 0 if the maximum concurrency is not restricted.
    int max_concurrency() const {
        if (NULL == _cl) {
            return 0;
        } else {
            return _cl->MaxConcurrency();
        }
    }

private:
friend class ScopedMethodStatus;
friend class Server;
    DISALLOW_COPY_AND_ASSIGN(MethodStatus);

    // Note: Following methods are not thread safe and can only be called 
    // before the server is started. 
    int& max_concurrency_ref();
    void SetConcurrencyLimiter(ConcurrencyLimiter* cl);

    ConcurrencyLimiter* _cl;
    bvar::Adder<int64_t>  _nerror;
    bvar::LatencyRecorder _latency_rec;
    bvar::PassiveStatus<int>  _nprocessing_bvar;
    bvar::Adder<uint32_t> _nrefused_bvar;
    bvar::Window<bvar::Adder<uint32_t>> _nrefused_per_second;
    butil::atomic<int> BAIDU_CACHELINE_ALIGNMENT _nprocessing;
};

class ScopedMethodStatus {
public:
    ScopedMethodStatus(MethodStatus* status, 
                       Controller* c, 
                       int64_t start_parse_us)
        : _status(status) 
        , _c(c)
        , _start_parse_us(start_parse_us) {}
    ~ScopedMethodStatus();
    operator MethodStatus* () const { return _status; }
private:
    DISALLOW_COPY_AND_ASSIGN(ScopedMethodStatus);
    MethodStatus* _status;
    Controller* _c;
    uint64_t _start_parse_us;
};

inline bool MethodStatus::OnRequested() {
    _nprocessing.fetch_add(1, butil::memory_order_relaxed);
    if (NULL == _cl || _cl->OnRequested()) {
        return true;
    } 
    _nrefused_bvar << 1;
    return false;
}

inline void MethodStatus::OnResponded(int error_code, int64_t latency) {
    _nprocessing.fetch_sub(1, butil::memory_order_relaxed);
    if (0 == error_code) {
        _latency_rec << latency;
    } else {
        _nerror << 1;
    }
    if (NULL != _cl) {
        _cl->OnResponded(error_code, latency);
    }
}

} // namespace brpc

#endif  //BRPC_METHOD_STATUS_H
