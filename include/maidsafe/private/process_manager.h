/* Copyright (c) 2009 maidsafe.net limited
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
    * Neither the name of the maidsafe.net limited nor the names of its
    contributors may be used to endorse or promote products derived from this
    software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef MAIDSAFE_PRIVATE_PROCESS_MANAGER_H_
#define MAIDSAFE_PRIVATE_PROCESS_MANAGER_H_

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/process.hpp>
#include <boost/thread/pthread/mutex.hpp>
#include <thread>
#include <string>
#include <vector>

namespace maidsafe {

namespace bp = boost::process;

enum class ProcessStatus {
  Running,
  Stopped,
  Crashed
};

enum ProcessInstruction {
  kRun = 1,
  kStop = 2,
  kTerminate = 3,
  kInvalid = 4
};

/*enum class TerminateStatus {
  kTerminate = 1,
  kNoTerminate = 2
};

enum class StopStatus {
  kStop = 1,
  kNoStop = 2
};*/


struct ProcessManagerStruct {
  ProcessInstruction instruction;
};

class Process {
 public:
  Process() : args_(), process_name_() {}
  bool SetProcessName(std::string process_name);
  void AddArgument(std::string argument);
  std::string ProcessName() const;
  std::vector<std::string> Args() const;
 private:
  std::vector<std::string> args_;
  std::string process_name_;
};

struct ProcessInfo {
  ProcessInfo() : process(), thread(), id(0), restart_count(0), done(false) {}
  // move constructor
  ProcessInfo(ProcessInfo&& other);
  ProcessInfo& operator=(ProcessInfo&& other);
  Process process;
  std::thread thread;
  int32_t id;
  int32_t restart_count;
  bool done;
};

class ProcessManager {
 public:
  ProcessManager();
  ~ProcessManager();
  int AddProcess(Process process);
  int32_t NumberOfProcesses();
  int32_t NumberOfLiveProcesses();
  int32_t NumberOfSleepingProcesses();
  void StopAndRemoveProcess(Process &process);
  ProcessStatus GetProcessStatus(Process &process);
  void StartProcess(int32_t id);
  void LetProcessDie(int32_t id);
  void WaitForProcesses();
  void KillProcess(int32_t id);
  void StopProcess(int32_t id);
  void RestartProcess(int32_t id);

 private:
  ProcessManager(const ProcessManager&);
  ProcessManager &operator=(const ProcessManager&);
  std::vector<ProcessInfo>::iterator FindProcess(int32_t num);
  void RunProcess(int32_t id, bool restart, bool logging);
  void RunAll();
  void MonitorAll();
  void TerminateAll();
  bool AddStatus(int32_t id, ProcessManagerStruct status);
  bool SetInstruction(int32_t id, ProcessInstruction instruction);
  ProcessInstruction CheckInstruction(int32_t id);
  std::vector<ProcessInfo> processes_;
  boost::mutex process_info_mutex_;
  uint32_t process_count_;
  bool done_;
  int32_t process_id_;
  std::string shared_mem_name_;
  boost::interprocess::managed_shared_memory shared_mem_;
};

}  // namespace maidsafe

#endif  // MAIDSAFE_PRIVATE_PROCESS_MANAGER_H_
