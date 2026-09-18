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

// ti: unified TIANSHU CLI entry (kubectl/docker plugin pattern).
//
//   ti <verb> [args...]  ->  exec ti-<verb> [args...]
//
// Tools are independent ti-* binaries on PATH; ti only dispatches and
// never reimplements them. A new tool joins the unified entry for free.

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>

namespace {

bool is_executable_on_path(const std::string& name, std::string* found) {
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return false;
  }
  std::string dir;
  std::size_t pos = 0;
  const std::string paths(path_env);
  while (pos <= paths.size()) {
    const std::size_t colon = paths.find(':', pos);
    dir = paths.substr(pos, colon == std::string::npos ? std::string::npos : colon - pos);
    std::string candidate = dir;
    candidate += "/";
    candidate += name;
    struct stat st{};
    if (!dir.empty() && stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
        access(candidate.c_str(), X_OK) == 0) {
      *found = candidate;
      return true;
    }
    if (colon == std::string::npos) {
      break;
    }
    pos = colon + 1;
  }
  return false;
}

// Discovery stays cheap: probe the planned tool families on PATH instead
// of scanning directories.
std::vector<std::string> discover_ti_tools() {
  std::vector<std::string> verbs;
  std::string unused;
  for (const char* probe : {"launch", "monitor", "compile", "info", "console", "ctl", "inspect"}) {
    if (is_executable_on_path(std::string("ti-") + probe, &unused)) {
      verbs.emplace_back(probe);
    }
  }
  return verbs;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    static_cast<void>(std::printf("ti - TIANSHU unified CLI\n\nusage: ti <verb> [args...]\n\n"));
    const auto verbs = discover_ti_tools();
    if (!verbs.empty()) {
      std::printf("available:\n");
      for (const auto& v : verbs) {
        static_cast<void>(std::printf("  ti %s\n", v.c_str()));
      }
    }
    return 2;
  }

  const std::string verb(argv[1]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  if (verb.find('/') != std::string::npos || verb.find('.') != std::string::npos || verb.empty() ||
      verb.front() == '-') {
    static_cast<void>(std::fprintf(stderr, "ti: invalid verb '%s'\n", verb.c_str()));
    return 2;
  }

  std::string tool = "ti-";
  tool += verb;
  std::vector<char*> child_argv;
  child_argv.push_back(const_cast<char*>(tool.c_str()));
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  for (int i = 2; i < argc; ++i) {
    child_argv.push_back(argv[i]);
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  child_argv.push_back(nullptr);

  const int rc = execvp(tool.c_str(), child_argv.data());  // NOLINT(concurrency-mt-unsafe)
  if (rc == -1) {
    static_cast<void>(std::fprintf(stderr, "ti: '%s' not found (looked for %s on PATH)\n",
                                   verb.c_str(), tool.c_str()));
    return 127;
  }
  return 0;
}
