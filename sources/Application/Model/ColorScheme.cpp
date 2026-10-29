#include "ColorScheme.h"
#include "Application/AppWindow.h"
#include "Application/Utils/char.h"
#include "Externals/TinyXML/tinyxml.h"
#include "System/Console/Trace.h"
#include <stdio.h>
#include <string.h>

static const ColorSchemeEntry kEntries[] = {
    {"BACKGROUND", "BACKGROUND", CD_BACKGROUND},
    {"FOREGROUND", "FOREGROUND", CD_NORMAL},
    {"BORDER", "BORDER", CD_BORDER},
    {"HICOLOR1", "HICOLOR1", CD_HILITE1},
    {"HICOLOR2", "HICOLOR2", CD_HILITE2},
    {"CONSOLE", "CONSOLE", CD_CONSOLE},
    {"CURSORCOLOR", "CURSORCOLOR", CD_CURSOR},
    {"PLAYCOLOR", "PLAYCOLOR", CD_PLAY},
    {"MUTECOLOR", "MUTECOLOR", CD_MUTE},
    {"SONGVIEW_FE", "SONGVIEW_FE", CD_SONGVIEWFE},
    {"SONGVIEW_00", "SONGVIEW_00", CD_SONGVIEW00},
    {"ROWCOLOR1", "ROWCOLOR1", CD_ROW},
    {"ROWCOLOR2", "ROWCOLOR2", CD_ROW2},
    {"MAJORBEAT", "MAJORBEAT", CD_MAJORBEAT},
    {"COL_TITLE", "COL_TITLE", CD_COL_TITLE},
};

const ColorSchemeEntry *ColorScheme::Entries() { return kEntries; }

int ColorScheme::EntryCount() {
    return (int)(sizeof(kEntries) / sizeof(kEntries[0]));
}

Path ColorScheme::SchemesDir() { return Path("root:schemes"); }

bool ColorScheme::EnsureSchemesDir() {
    Path dir = SchemesDir();
    std::string resolved = dir.GetPath();
    Path resolvedPath(resolved);
    if (resolvedPath.Exists() && resolvedPath.IsDirectory())
        return true;
    Result r = FileSystem::GetInstance()->MakeDir(resolved.c_str());
    return !r.Failed();
}

static void colorToHex(const GUIColor &c, char *out6) {
    char h[3];
    hex2char((unsigned char)(c._r & 0xFF), h);
    out6[0] = h[0];
    out6[1] = h[1];
    hex2char((unsigned char)(c._g & 0xFF), h);
    out6[2] = h[0];
    out6[3] = h[1];
    hex2char((unsigned char)(c._b & 0xFF), h);
    out6[4] = h[0];
    out6[5] = h[1];
    out6[6] = 0;
}

static bool hexToColor(const char *value, GUIColor &color) {
    if (!value || strlen(value) < 6)
        return false;
    unsigned char r, g, b;
    char2hex(value, &r);
    char2hex(value + 2, &g);
    char2hex(value + 4, &b);
    color = GUIColor(r, g, b);
    return true;
}

#define LAST_SCHEME_NAME "bin:last_scheme"

static bool loadSchemeFile(const char *name) {
    if (!name || !name[0])
        return false;
    Path file = ColorScheme::SchemesDir().Descend(std::string(name) + ".xml");
    TiXmlDocument doc(file.GetPath());
    if (!doc.LoadFile()) {
        Trace::Error("ColorScheme: failed to load %s", file.GetPath().c_str());
        return false;
    }
    TiXmlNode *rootnode = doc.FirstChild("CONFIG");
    if (!rootnode)
        rootnode = doc.FirstChild("SCHEME");
    if (!rootnode)
        return false;

    TiXmlElement *root = rootnode->ToElement();
    for (int i = 0; i < ColorScheme::EntryCount(); i++) {
        const ColorSchemeEntry &entry = ColorScheme::Entries()[i];
        TiXmlElement *el = root->FirstChildElement(entry.configKey);
        if (!el)
            continue;
        const char *value = el->Attribute("value");
        if (!value)
            value = el->Attribute("VALUE");
        GUIColor color(0, 0, 0);
        if (hexToColor(value, color))
            AppWindow::SetPaletteColor(entry.cd, color);
    }
    return true;
}

void ColorScheme::RememberLast(const char *name) {
    if (!name || !name[0])
        return;
    Path lastFile(LAST_SCHEME_NAME);
    FileSystem *fs = FileSystem::GetInstance();
    I_File *file = fs->Open(lastFile.GetPath().c_str(), "w");
    if (!file) {
        Trace::Error("ColorScheme: cannot write %s", LAST_SCHEME_NAME);
        return;
    }
    file->Write(name, 1, strlen(name));
    file->Write("\n", 1, 1);
    file->Close();
    delete file;
    Trace::Log("ColorScheme", "Remembered last scheme: %s", name);
}

bool ColorScheme::ApplyLast() {
    Path lastFile(LAST_SCHEME_NAME);
    FileSystem *fs = FileSystem::GetInstance();
    I_File *file = fs->Open(lastFile.GetPath().c_str(), "r");
    if (!file)
        return false;

    char buffer[128];
    memset(buffer, 0, sizeof(buffer));
    int n = file->Read(buffer, 1, sizeof(buffer) - 1);
    file->Close();
    delete file;
    if (n <= 0)
        return false;

    // trim trailing whitespace / newline
    while (n > 0 && (buffer[n - 1] == '\n' || buffer[n - 1] == '\r' ||
                     buffer[n - 1] == ' ')) {
        buffer[--n] = 0;
    }
    if (!buffer[0])
        return false;

    if (!loadSchemeFile(buffer)) {
        Trace::Error("ColorScheme: last scheme '%s' missing, keeping defaults",
                     buffer);
        return false;
    }
    Trace::Log("ColorScheme", "Applied last scheme: %s", buffer);
    return true;
}

bool ColorScheme::Save(const char *name) {
    if (!name || !name[0])
        return false;
    if (!EnsureSchemesDir()) {
        Trace::Error("ColorScheme: cannot create schemes dir");
        return false;
    }

    Path file = SchemesDir().Descend(std::string(name) + ".xml");
    TiXmlDocument doc;
    TiXmlElement *root = new TiXmlElement("CONFIG");
    for (int i = 0; i < EntryCount(); i++) {
        char hex[8];
        colorToHex(AppWindow::GetPaletteColor(kEntries[i].cd), hex);
        TiXmlElement *el = new TiXmlElement(kEntries[i].configKey);
        el->SetAttribute("value", hex);
        root->LinkEndChild(el);
    }
    doc.LinkEndChild(root);
    if (!doc.SaveFile(file.GetPath().c_str())) {
        Trace::Error("ColorScheme: failed to save %s", file.GetPath().c_str());
        return false;
    }
    Trace::Log("ColorScheme", "Saved %s", file.GetPath().c_str());
    RememberLast(name);
    return true;
}

bool ColorScheme::Load(const char *name) {
    if (!loadSchemeFile(name))
        return false;
    RememberLast(name);
    AppWindow *w = AppWindow::GetInstance();
    if (w)
        w->InvalidatePalette();
    return true;
}

void ColorScheme::ListSchemes(std::vector<std::string> &out) {
    out.clear();
    Path dir = SchemesDir();
    std::string resolved = dir.GetPath();
    Path resolvedPath(resolved);
    if (!resolvedPath.Exists() || !resolvedPath.IsDirectory())
        return;
    I_Dir *d = FileSystem::GetInstance()->Open(resolved.c_str());
    if (!d)
        return;
    d->GetContent("*.xml");
    d->Sort();
    IteratorPtr<Path> it(d->GetIterator());
    for (it->Begin(); !it->IsDone(); it->Next()) {
        Path &p = it->CurrentItem();
        if (!p.IsFile())
            continue;
        std::string n = p.GetName();
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".xml") == 0)
            n = n.substr(0, n.size() - 4);
        out.push_back(n);
    }
    delete d;
}
