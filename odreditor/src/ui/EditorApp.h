#pragma once
#include <string>

namespace odreditor
{
class EditorApp
{
public:
    void Draw();

private:
    float       TreeWidth = 250;
    float       InspectorWidth = 310;
    std::string FileName;
    std::string Status = "Ready";
    bool        ShowNotices = false;
};
} // namespace odreditor
