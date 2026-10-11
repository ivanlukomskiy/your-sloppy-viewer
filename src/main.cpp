#include "Window.h"

#include <filesystem>
#include <initializer_list>
#include <vector>

namespace
{
void setAccels(GtkApplication* application, const char* action, std::initializer_list<const char*> keys)
{
  std::vector<const char*> list(keys);
  list.push_back(nullptr);
  gtk_application_set_accels_for_action(application, action, list.data());
}

void onStartup(GApplication* application, gpointer)
{
  adw_style_manager_set_color_scheme(adw_style_manager_get_default(), ADW_COLOR_SCHEME_FORCE_DARK);

  GtkApplication* gtkApplication = GTK_APPLICATION(application);
  setAccels(gtkApplication, "win.open", {"<Control>o"});
  setAccels(gtkApplication, "win.close", {"Escape", "<Control>q", "<Control>w"});
  setAccels(gtkApplication, "win.fit", {"f"});
  setAccels(gtkApplication, "win.toggle-edges", {"e"});
  setAccels(gtkApplication, "win.toggle-wireframe", {"w"});
  setAccels(gtkApplication, "win.perspective", {"p"});
  setAccels(gtkApplication, "win.cube", {"c"});
  setAccels(gtkApplication, "win.dimensions", {"d"});
  setAccels(gtkApplication, "win.layers", {"l"});
  setAccels(gtkApplication, "win.view::front", {"1", "KP_1"});
  setAccels(gtkApplication, "win.view::right", {"2", "KP_2"});
  setAccels(gtkApplication, "win.view::top", {"3", "KP_3"});
  setAccels(gtkApplication, "win.view::iso", {"0", "KP_0"});
  setAccels(gtkApplication, "win.shortcuts", {"<Control>question"});
}

void onActivate(GApplication* application, gpointer)
{
  gtk_window_present(createViewerWindow(ADW_APPLICATION(application), {}));
}

void onOpen(GApplication* application, GFile** files, int count, const char*, gpointer)
{
  std::filesystem::path path;
  if (count > 0)
  {
    g_autofree char* name = g_file_get_path(files[0]);
    if (name != nullptr)
    {
      path = name;
    }
  }
  gtk_window_present(createViewerWindow(ADW_APPLICATION(application), path));
}
}

int main(int argc, char** argv)
{
  g_autoptr(AdwApplication) application = adw_application_new(
    kAppId, static_cast<GApplicationFlags>(G_APPLICATION_HANDLES_OPEN | G_APPLICATION_NON_UNIQUE));
  g_signal_connect(application, "startup", G_CALLBACK(onStartup), nullptr);
  g_signal_connect(application, "activate", G_CALLBACK(onActivate), nullptr);
  g_signal_connect(application, "open", G_CALLBACK(onOpen), nullptr);
  return g_application_run(G_APPLICATION(application), argc, argv);
}
