#include "modules/terminal/venv.h"

namespace terminal {

modules::FolderProbeRequest BuildVenvProbe(const LaunchSpec& spec) {
  modules::FolderProbeRequest request;
  request.directory = spec.working_folder;
  request.relative_files = spec.target == Target::Wsl
                               ? std::vector<std::wstring>{L".venv\\bin\\activate"}
                               : std::vector<std::wstring>{
                                     L".venv\\Scripts\\Activate.ps1"};
  return request;
}

bool HasCompatibleVenv(const LaunchSpec& spec,
                       const modules::FolderProbeResult& result) {
  const std::wstring expected = spec.target == Target::Wsl
                                    ? L".venv\\bin\\activate"
                                    : L".venv\\Scripts\\Activate.ps1";
  for (const modules::RelativeFilePresence& file : result.files) {
    if (file.relative_path == expected) return file.present;
  }
  return false;
}

modules::ProcessRequest BuildVenvCreateRequest(const LaunchSpec& spec,
                                               bool python_fallback) {
  modules::ProcessRequest request;
  request.operation = modules::ProcessOperation::Capture;
  request.working_directory = spec.working_folder;
  request.timeout_ms = 120000;
  if (spec.target == Target::Wsl) {
    request.executable = L"wsl.exe";
    request.arguments = {L"-d", spec.wsl_distro, L"--cd", spec.working_folder,
                         L"--", L"python3", L"-m", L"venv", L".venv"};
  } else if (python_fallback) {
    request.executable = L"python.exe";
    request.arguments = {L"-m", L"venv", L".venv"};
  } else {
    request.executable = L"py.exe";
    request.arguments = {L"-3", L"-m", L"venv", L".venv"};
  }
  return request;
}

}  // namespace terminal
