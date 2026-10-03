#pragma once

// deadbeef.h includes <dirent.h> to declare a scandir wrapper this plugin never
// calls; the type only appears behind pointers, so a declaration is enough.

struct dirent;
