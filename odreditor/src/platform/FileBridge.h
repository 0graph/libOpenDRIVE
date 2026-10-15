#pragma once
#include <optional>
#include <string>

namespace odreditor
{
struct OpenedFile
{
    std::string Name;
    std::string Bytes;
    std::string Error = {};
};
void                      InitializeFileBridge();
void                      ChooseFile();
std::optional<OpenedFile> TakeOpenedFile();
void                      DownloadFile(const std::string& name, const std::string& bytes);
void                      SetModified(bool modified);
} // namespace odreditor
