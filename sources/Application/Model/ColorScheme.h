#ifndef _COLOR_SCHEME_H_
#define _COLOR_SCHEME_H_

#include "Application/Views/BaseClasses/View.h"
#include "System/FileSystem/FileSystem.h"
#include "UIFramework/Framework/GUIColor.h"
#include <string>
#include <vector>

struct ColorSchemeEntry {
    const char *configKey;
    const char *label;
    ColorDefinition cd;
};

class ColorScheme {
  public:
    static const ColorSchemeEntry *Entries();
    static int EntryCount();

    static Path SchemesDir();
    static bool EnsureSchemesDir();

    static bool Save(const char *name);
    static bool Load(const char *name);

    static void ListSchemes(std::vector<std::string> &out);

    // Persist / restore the last loaded or saved scheme (bin:last_scheme)
    static void RememberLast(const char *name);
    static bool ApplyLast();
};

#endif
