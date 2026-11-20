#include "UiScript.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "Settings.h"

namespace Frontend
{

bool LoadUiScript(const std::string& path, std::vector<UiStep>& steps)
{
    std::ifstream file(PathFromUtf8(path));
    if (!file)
    {
        fprintf(stderr, "can't read the UI script %s\n", path.c_str());
        return false;
    }
    static const char* const kActions[] = {"up", "down", "left", "right", "confirm", "back", "tab-next", "tab-prev", "menu"};
    std::string line;
    int number = 0;
    bool ok = true;
    while (std::getline(file, line))
    {
        number++;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#')
            continue;
        line = line.substr(start);
        size_t space = line.find(' ');
        std::string word = line.substr(0, space);
        std::string rest = space == std::string::npos ? "" : line.substr(space + 1);
        // trailing blanks off the argument, except for text, which is as written
        if (word != "text")
            while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t'))
                rest.pop_back();
        UiStep step {};
        step.Line = number;
        step.Text = line;
        bool known = true;
        bool action = false;
        for (const char* a : kActions)
            action |= word == a;
        if (action)
        {
            step.Type = UiStep::Kind::Action;
            step.Arg = word;
        }
        else if (word == "text") { step.Type = UiStep::Kind::Text; step.Arg = rest; }
        else if (word == "key" && !rest.empty()) { step.Type = UiStep::Kind::Key; step.Arg = rest; }
        else if (word == "pad" && !rest.empty()) { step.Type = UiStep::Kind::Pad; step.Arg = rest; }
        else if ((word == "wait" || word == "frame") && !rest.empty())
        {
            step.Type = word == "wait" ? UiStep::Kind::Wait : UiStep::Kind::Frame;
            char* end = nullptr;
            step.Number = (unsigned)strtoul(rest.c_str(), &end, 10);
            known = end && *end == 0;
        }
        else if (word == "shot" && !rest.empty()) { step.Type = UiStep::Kind::Shot; step.Arg = rest; }
        else if (word == "screenshot" && !rest.empty()) { step.Type = UiStep::Kind::Screenshot; step.Arg = rest; }
        else if (word == "quit") step.Type = UiStep::Kind::Quit;
        else
            known = false;
        if (!known)
        {
            fprintf(stderr, "%s:%d: not a UI script step: %s\n", path.c_str(), number, line.c_str());
            ok = false;
            continue;
        }
        steps.push_back(step);
    }
    return ok;
}

}
