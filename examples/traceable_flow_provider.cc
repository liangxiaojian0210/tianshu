// Copyright 2026 Pride Leong.
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

// Provider .so for the PoC demo walkthrough (demo/poc-demo.sh): registers
// the demo flows at static init so ti-compile/ti-launch resolve them by
// name only after dlopening this library via --flows (the flow registry
// is process-local). Shares its declarations with
// examples/traceable_flow_demo.cc through traceable_flow_decls.h.

#include "tianshu/dsl/flow.h"

#include "traceable_flow_decls.h"

REGISTER_TRACEABLE_FLOW("demo_traceable", declare_demo_flow)
REGISTER_TRACEABLE_FLOW("demo_traceable_lite", declare_demo_flow_lite)
