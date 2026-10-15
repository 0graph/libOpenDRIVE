#include "FileBridge.h"
#include <emscripten.h>
#include <utility>

namespace
{
std::optional<odreditor::OpenedFile> PendingFile;
}

extern "C" EMSCRIPTEN_KEEPALIVE void ReceiveFile(const char* name, const char* bytes, int size)
{
    // Copy before ccall releases its temporary UTF-8 and byte buffers.
    PendingFile = odreditor::OpenedFile{name, std::string(bytes, size)};
}

namespace odreditor
{
void InitializeFileBridge()
{
    EM_ASM({ Module.initializeFileBridge(); });
}

void ChooseFile()
{
    EM_ASM({ document.getElementById('file-input').click(); });
}

std::optional<OpenedFile> TakeOpenedFile()
{
    auto result = std::move(PendingFile);
    PendingFile.reset();
    return result;
}

void DownloadFile(const std::string& name, const std::string& bytes)
{
    EM_ASM({ Module.downloadFile($0, $1, $2); }, name.c_str(), bytes.data(), bytes.size());
}

void SetModified(bool modified)
{
    EM_ASM({ Module.documentModified = !!$0; }, modified);
}
} // namespace odreditor
