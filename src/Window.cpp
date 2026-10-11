#include "Window.h"

#include "Camera.h"
#include "Renderer.h"
#include "Loader.h"
#include "TextAtlas.h"

#include <epoxy/gl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr double kClickSlop = 4.0;

const char* kStyleSheet = R"(
.loading-card {
  padding: 14px 18px;
  border-radius: 14px;
  background-color: alpha(black, 0.55);
}
.dimensions-card {
  padding: 10px 14px;
  border-radius: 12px;
  background-color: alpha(black, 0.45);
  font-feature-settings: "tnum";
}
.viewport-status {
  background: none;
}
)";

struct LoadJob
{
  std::filesystem::path path;
  std::string name;
  LoadResult result;
  LoadProgress progress;
  std::jthread thread;
};

class ViewerWindow
{
public:
  ViewerWindow(AdwApplication* application, const std::filesystem::path& initialFile);

  GtkWindow* window() const { return GTK_WINDOW(window_); }

private:
  void buildHeader(GtkWidget* toolbarView);
  void buildContent(GtkWidget* toolbarView);
  void installActions();
  void loadSettings();
  void saveSettings() const;

  void open(const std::filesystem::path& path);
  void startLoad(const std::filesystem::path& path);
  void finishLoad();
  void updateProgress();
  void showScene();
  void updateTitle();
  void redraw() { gtk_gl_area_queue_render(GTK_GL_AREA(area_)); }
  void uploadCubeLabels();
  void updateDimensions();
  void applyModelKind();

  void setStyle(Renderer::Style style);
  const char* styleName() const;

  static void onRealize(GtkGLArea* area, gpointer self);
  static void onUnrealize(GtkGLArea* area, gpointer self);
  static gboolean onRender(GtkGLArea* area, GdkGLContext* context, gpointer self);
  static void onDragBegin(GtkGestureDrag* gesture, double x, double y, gpointer self);
  static void onDragUpdate(GtkGestureDrag* gesture, double x, double y, gpointer self);
  static void onDragEnd(GtkGestureDrag* gesture, double x, double y, gpointer self);
  static gboolean onScroll(GtkEventControllerScroll* controller, double dx, double dy, gpointer self);
  static void onMotion(GtkEventControllerMotion* controller, double x, double y, gpointer self);
  static void onZoomBegin(GtkGesture* gesture, GdkEventSequence* sequence, gpointer self);
  static void onZoomChanged(GtkGestureZoom* gesture, double scale, gpointer self);
  static gboolean onDrop(GtkDropTarget* target, const GValue* value, double x, double y, gpointer self);
  static gboolean onCloseRequest(GtkWindow* window, gpointer self);
  static gboolean onProgressTick(gpointer self);
  static gboolean onLoadDone(gpointer self);
  static void onFileChosen(GObject* source, GAsyncResult* result, gpointer self);

  static void actionOpen(GSimpleAction*, GVariant*, gpointer self);
  static void actionClose(GSimpleAction*, GVariant*, gpointer self);
  static void actionFit(GSimpleAction*, GVariant*, gpointer self);
  static void actionView(GSimpleAction*, GVariant* parameter, gpointer self);
  static void actionToggleEdges(GSimpleAction*, GVariant*, gpointer self);
  static void actionToggleWireframe(GSimpleAction*, GVariant*, gpointer self);
  static void actionShortcuts(GSimpleAction*, GVariant*, gpointer self);
  static void actionAbout(GSimpleAction*, GVariant*, gpointer self);
  static void changeStyle(GSimpleAction* action, GVariant* value, gpointer self);
  static void changeCube(GSimpleAction* action, GVariant* value, gpointer self);
  static void changePerspective(GSimpleAction* action, GVariant* value, gpointer self);
  static void changeDimensions(GSimpleAction* action, GVariant* value, gpointer self);

  GtkWidget* window_ = nullptr;
  GtkWidget* title_ = nullptr;
  GtkWidget* toasts_ = nullptr;
  GtkWidget* area_ = nullptr;
  GtkWidget* emptyPage_ = nullptr;
  GtkWidget* loadingCard_ = nullptr;
  GtkWidget* loadingLabel_ = nullptr;
  GtkWidget* loadingBar_ = nullptr;
  GtkWidget* dimensionsCard_ = nullptr;
  GtkWidget* dimensionValues_[3] = {};
  GtkWidget* dimensionNames_[3] = {};

  Renderer renderer_;
  Camera camera_;
  Renderer::Style style_ = Renderer::Style::ShadedEdges;
  bool cube_ = true;
  bool perspective_ = false;
  bool dimensions_ = false;
  bool realized_ = false;
  bool fitPending_ = false;
  bool hasModel_ = false;
  bool flat_ = false;
  std::string modelUnit_ = "mm";

  std::optional<Scene> pendingScene_;
  std::unique_ptr<LoadJob> job_;
  std::filesystem::path queued_;
  std::string currentName_;
  std::array<double, 3> modelSize_{};
  guint progressTimer_ = 0;

  double cursorX_ = 0.0;
  double cursorY_ = 0.0;
  double dragLastX_ = 0.0;
  double dragLastY_ = 0.0;
  double dragStartX_ = 0.0;
  double dragStartY_ = 0.0;
  double zoomLast_ = 1.0;
  guint dragButton_ = 0;
};

ViewerWindow* from(gpointer self)
{
  return static_cast<ViewerWindow*>(self);
}

std::filesystem::path settingsPath()
{
  return std::filesystem::path(g_get_user_config_dir()) / "your-sloppy-viewer" / "settings.ini";
}

ViewerWindow::ViewerWindow(AdwApplication* application, const std::filesystem::path& initialFile)
{
  loadSettings();
  camera_.setPerspective(perspective_);

  window_ = adw_application_window_new(GTK_APPLICATION(application));
  gtk_window_set_default_size(GTK_WINDOW(window_), 1100, 720);
  gtk_window_set_icon_name(GTK_WINDOW(window_), kAppId);
  g_object_set_data_full(G_OBJECT(window_), "viewer", this,
                         [](gpointer self) { delete from(self); });
  g_signal_connect(window_, "close-request", G_CALLBACK(onCloseRequest), this);

  GtkCssProvider* css = gtk_css_provider_new();
  gtk_css_provider_load_from_string(css, kStyleSheet);
  gtk_style_context_add_provider_for_display(gtk_widget_get_display(window_), GTK_STYLE_PROVIDER(css),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(css);

  GtkWidget* toolbarView = adw_toolbar_view_new();
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_), toolbarView);
  installActions();
  buildHeader(toolbarView);
  buildContent(toolbarView);
  updateTitle();

  if (!initialFile.empty())
  {
    open(initialFile);
  }
}

void ViewerWindow::buildHeader(GtkWidget* toolbarView)
{
  GtkWidget* header = adw_header_bar_new();
  title_ = adw_window_title_new(kAppName, nullptr);
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title_);

  GtkWidget* openButton = gtk_button_new_with_label("Open");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(openButton), "win.open");
  gtk_widget_set_tooltip_text(openButton, "Open a file (Ctrl+O)");
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), openButton);

  g_autoptr(GMenu) mainMenu = g_menu_new();
  g_menu_append(mainMenu, "Keyboard Shortcuts", "win.shortcuts");
  g_menu_append(mainMenu, "About Your Sloppy Viewer", "win.about");
  GtkWidget* mainButton = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(mainButton), "open-menu-symbolic");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(mainButton), G_MENU_MODEL(mainMenu));
  gtk_menu_button_set_primary(GTK_MENU_BUTTON(mainButton), TRUE);
  gtk_widget_set_tooltip_text(mainButton, "Main Menu");
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), mainButton);

  g_autoptr(GMenu) styles = g_menu_new();
  g_menu_append(styles, "Shaded with Edges", "win.style::shaded-edges");
  g_menu_append(styles, "Shaded", "win.style::shaded");
  g_menu_append(styles, "Wireframe", "win.style::wireframe");
  g_autoptr(GMenu) toggles = g_menu_new();
  g_menu_append(toggles, "Perspective", "win.perspective");
  g_menu_append(toggles, "Orientation Cube", "win.cube");
  g_menu_append(toggles, "Dimensions", "win.dimensions");
  g_autoptr(GMenu) commands = g_menu_new();
  g_menu_append(commands, "Fit to Window", "win.fit");
  g_autoptr(GMenu) viewMenu = g_menu_new();
  g_menu_append_section(viewMenu, "Render Style", G_MENU_MODEL(styles));
  g_menu_append_section(viewMenu, nullptr, G_MENU_MODEL(toggles));
  g_menu_append_section(viewMenu, nullptr, G_MENU_MODEL(commands));

  GtkWidget* viewButton = gtk_menu_button_new();
  gtk_menu_button_set_label(GTK_MENU_BUTTON(viewButton), "View");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(viewButton), G_MENU_MODEL(viewMenu));
  gtk_widget_set_tooltip_text(viewButton, "View Options");
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), viewButton);

  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbarView), header);
}

void ViewerWindow::buildContent(GtkWidget* toolbarView)
{
  area_ = gtk_gl_area_new();
  gtk_gl_area_set_allowed_apis(GTK_GL_AREA(area_), GDK_GL_API_GL);
  gtk_gl_area_set_required_version(GTK_GL_AREA(area_), 3, 3);
  gtk_gl_area_set_auto_render(GTK_GL_AREA(area_), FALSE);
  gtk_widget_set_hexpand(area_, TRUE);
  gtk_widget_set_vexpand(area_, TRUE);
  g_signal_connect(area_, "realize", G_CALLBACK(onRealize), this);
  g_signal_connect(area_, "unrealize", G_CALLBACK(onUnrealize), this);
  g_signal_connect(area_, "render", G_CALLBACK(onRender), this);

  GtkGesture* drag = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), 0);
  g_signal_connect(drag, "drag-begin", G_CALLBACK(onDragBegin), this);
  g_signal_connect(drag, "drag-update", G_CALLBACK(onDragUpdate), this);
  g_signal_connect(drag, "drag-end", G_CALLBACK(onDragEnd), this);
  gtk_widget_add_controller(area_, GTK_EVENT_CONTROLLER(drag));

  GtkEventController* scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect(scroll, "scroll", G_CALLBACK(onScroll), this);
  gtk_widget_add_controller(area_, scroll);

  GtkEventController* motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "motion", G_CALLBACK(onMotion), this);
  gtk_widget_add_controller(area_, motion);

  GtkGesture* zoom = gtk_gesture_zoom_new();
  g_signal_connect(zoom, "begin", G_CALLBACK(onZoomBegin), this);
  g_signal_connect(zoom, "scale-changed", G_CALLBACK(onZoomChanged), this);
  gtk_widget_add_controller(area_, GTK_EVENT_CONTROLLER(zoom));

  emptyPage_ = adw_status_page_new();
  const bool hasAppIcon =
    gtk_icon_theme_has_icon(gtk_icon_theme_get_for_display(gtk_widget_get_display(window_)), kAppId);
  adw_status_page_set_icon_name(ADW_STATUS_PAGE(emptyPage_),
                                hasAppIcon ? kAppId : "document-open-symbolic");
  adw_status_page_set_title(ADW_STATUS_PAGE(emptyPage_), "Open a STEP or DXF File");
  adw_status_page_set_description(ADW_STATUS_PAGE(emptyPage_),
                                  "Drag and drop a file here, or choose one");
  GtkWidget* emptyButton = gtk_button_new_with_label("Open…");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(emptyButton), "win.open");
  gtk_widget_set_halign(emptyButton, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class(emptyButton, "pill");
  gtk_widget_add_css_class(emptyButton, "suggested-action");
  adw_status_page_set_child(ADW_STATUS_PAGE(emptyPage_), emptyButton);
  gtk_widget_add_css_class(emptyPage_, "viewport-status");

  loadingLabel_ = gtk_label_new(nullptr);
  gtk_label_set_xalign(GTK_LABEL(loadingLabel_), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(loadingLabel_), PANGO_ELLIPSIZE_MIDDLE);
  loadingBar_ = gtk_progress_bar_new();
  gtk_widget_set_size_request(loadingBar_, 280, -1);
  gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(loadingBar_), 0.06);
  loadingCard_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_box_append(GTK_BOX(loadingCard_), loadingLabel_);
  gtk_box_append(GTK_BOX(loadingCard_), loadingBar_);
  gtk_widget_add_css_class(loadingCard_, "loading-card");
  gtk_widget_set_margin_start(loadingCard_, 16);
  gtk_widget_set_margin_bottom(loadingCard_, 16);
  gtk_widget_set_visible(loadingCard_, FALSE);

  GtkWidget* dimensionsGrid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(dimensionsGrid), 12);
  gtk_grid_set_row_spacing(GTK_GRID(dimensionsGrid), 2);
  const char* axes[3] = {"X", "Y", "Z"};
  for (int axis = 0; axis < 3; ++axis)
  {
    GtkWidget* name = gtk_label_new(axes[axis]);
    gtk_widget_add_css_class(name, "dim-label");
    dimensionNames_[axis] = name;
    dimensionValues_[axis] = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(dimensionValues_[axis]), 1.0f);
    gtk_widget_set_hexpand(dimensionValues_[axis], TRUE);
    gtk_grid_attach(GTK_GRID(dimensionsGrid), name, 0, axis, 1, 1);
    gtk_grid_attach(GTK_GRID(dimensionsGrid), dimensionValues_[axis], 1, axis, 1, 1);
  }
  dimensionsCard_ = dimensionsGrid;
  gtk_widget_add_css_class(dimensionsCard_, "dimensions-card");
  gtk_widget_set_halign(dimensionsCard_, GTK_ALIGN_START);
  gtk_widget_set_valign(dimensionsCard_, GTK_ALIGN_START);
  gtk_widget_set_margin_start(dimensionsCard_, 12);
  gtk_widget_set_margin_top(dimensionsCard_, 12);
  gtk_widget_set_can_target(dimensionsCard_, FALSE);
  gtk_widget_set_tooltip_text(dimensionsCard_, "Overall size of the model");
  gtk_widget_set_visible(dimensionsCard_, FALSE);

  GtkWidget* overlay = gtk_overlay_new();
  gtk_overlay_set_child(GTK_OVERLAY(overlay), area_);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), emptyPage_);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), dimensionsCard_);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), loadingCard_);

  GtkDropTarget* drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
  g_signal_connect(drop, "drop", G_CALLBACK(onDrop), this);
  gtk_widget_add_controller(overlay, GTK_EVENT_CONTROLLER(drop));

  toasts_ = adw_toast_overlay_new();
  adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(toasts_), overlay);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbarView), toasts_);
}

void ViewerWindow::installActions()
{
  const GActionEntry entries[] = {
    {"open", actionOpen, nullptr, nullptr, nullptr, {}},
    {"close", actionClose, nullptr, nullptr, nullptr, {}},
    {"fit", actionFit, nullptr, nullptr, nullptr, {}},
    {"view", actionView, "s", nullptr, nullptr, {}},
    {"toggle-edges", actionToggleEdges, nullptr, nullptr, nullptr, {}},
    {"toggle-wireframe", actionToggleWireframe, nullptr, nullptr, nullptr, {}},
    {"shortcuts", actionShortcuts, nullptr, nullptr, nullptr, {}},
    {"about", actionAbout, nullptr, nullptr, nullptr, {}},
    {"style", nullptr, "s", "'shaded-edges'", changeStyle, {}},
    {"cube", nullptr, nullptr, "true", changeCube, {}},
    {"perspective", nullptr, nullptr, "false", changePerspective, {}},
    {"dimensions", nullptr, nullptr, "false", changeDimensions, {}},
  };
  g_action_map_add_action_entries(G_ACTION_MAP(window_), entries, G_N_ELEMENTS(entries), this);

  g_action_group_change_action_state(G_ACTION_GROUP(window_), "style", g_variant_new_string(styleName()));
  g_action_group_change_action_state(G_ACTION_GROUP(window_), "cube", g_variant_new_boolean(cube_));
  g_action_group_change_action_state(G_ACTION_GROUP(window_), "perspective",
                                     g_variant_new_boolean(perspective_));
  g_action_group_change_action_state(G_ACTION_GROUP(window_), "dimensions",
                                     g_variant_new_boolean(dimensions_));
}

void ViewerWindow::loadSettings()
{
  g_autoptr(GKeyFile) file = g_key_file_new();
  if (!g_key_file_load_from_file(file, settingsPath().c_str(), G_KEY_FILE_NONE, nullptr))
  {
    return;
  }
  g_autofree char* style = g_key_file_get_string(file, "View", "style", nullptr);
  if (g_strcmp0(style, "shaded") == 0)
  {
    style_ = Renderer::Style::Shaded;
  }
  else if (g_strcmp0(style, "wireframe") == 0)
  {
    style_ = Renderer::Style::Wireframe;
  }
  if (g_key_file_has_key(file, "View", "cube", nullptr))
  {
    cube_ = g_key_file_get_boolean(file, "View", "cube", nullptr);
  }
  if (g_key_file_has_key(file, "View", "perspective", nullptr))
  {
    perspective_ = g_key_file_get_boolean(file, "View", "perspective", nullptr);
  }
  if (g_key_file_has_key(file, "View", "dimensions", nullptr))
  {
    dimensions_ = g_key_file_get_boolean(file, "View", "dimensions", nullptr);
  }
}

void ViewerWindow::saveSettings() const
{
  g_autoptr(GKeyFile) file = g_key_file_new();
  g_key_file_set_string(file, "View", "style", styleName());
  g_key_file_set_boolean(file, "View", "cube", cube_);
  g_key_file_set_boolean(file, "View", "perspective", perspective_);
  g_key_file_set_boolean(file, "View", "dimensions", dimensions_);

  const std::filesystem::path path = settingsPath();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  g_key_file_save_to_file(file, path.c_str(), nullptr);
}

const char* ViewerWindow::styleName() const
{
  switch (style_)
  {
    case Renderer::Style::Shaded:
      return "shaded";
    case Renderer::Style::Wireframe:
      return "wireframe";
    default:
      return "shaded-edges";
  }
}

void ViewerWindow::setStyle(Renderer::Style style)
{
  g_action_group_change_action_state(
    G_ACTION_GROUP(window_), "style",
    g_variant_new_string(style == Renderer::Style::Shaded      ? "shaded"
                         : style == Renderer::Style::Wireframe ? "wireframe"
                                                               : "shaded-edges"));
}

void ViewerWindow::open(const std::filesystem::path& path)
{
  if (job_)
  {
    queued_ = path;
    return;
  }
  startLoad(path);
}

void ViewerWindow::startLoad(const std::filesystem::path& path)
{
  job_ = std::make_unique<LoadJob>();
  job_->path = path;
  job_->name = path.filename().string();

  gtk_widget_set_visible(emptyPage_, FALSE);
  gtk_widget_set_halign(loadingCard_, hasModel_ ? GTK_ALIGN_START : GTK_ALIGN_CENTER);
  gtk_widget_set_valign(loadingCard_, hasModel_ ? GTK_ALIGN_END : GTK_ALIGN_CENTER);
  gtk_widget_set_visible(loadingCard_, TRUE);
  updateProgress();
  progressTimer_ = g_timeout_add(50, onProgressTick, this);

  job_->thread = std::jthread([this, job = job_.get()] {
    job->result = loadModel(job->path, job->progress);
    g_idle_add(onLoadDone, this);
  });
}

void ViewerWindow::updateProgress()
{
  const LoadProgress::Stage stage = job_->progress.stage.load();
  const char* verb = "Reading";
  switch (stage)
  {
    case LoadProgress::Stage::Reading:
      break;
    case LoadProgress::Stage::Parsing:
      verb = "Parsing";
      break;
    case LoadProgress::Stage::Converting:
      verb = "Converting";
      break;
    case LoadProgress::Stage::Tessellating:
      verb = "Tessellating";
      break;
    case LoadProgress::Stage::Preparing:
      verb = "Preparing";
      break;
  }

  const std::string text = std::string(verb) + " " + job_->name + "…";
  if (text != gtk_label_get_text(GTK_LABEL(loadingLabel_)))
  {
    gtk_label_set_text(GTK_LABEL(loadingLabel_), text.c_str());
  }
  if (stage == LoadProgress::Stage::Parsing)
  {
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(loadingBar_));
  }
  else
  {
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(loadingBar_),
                                  std::clamp(job_->progress.fraction.load(), 0.0f, 1.0f));
  }
}

void ViewerWindow::finishLoad()
{
  const std::unique_ptr<LoadJob> job = std::move(job_);
  job->thread.join();
  if (progressTimer_ != 0)
  {
    g_source_remove(progressTimer_);
    progressTimer_ = 0;
  }
  gtk_widget_set_visible(loadingCard_, FALSE);

  if (!job->result.scene)
  {
    std::fprintf(stderr, "your-sloppy-viewer: cannot open '%s': %s\n", job->path.c_str(),
                 job->result.error.c_str());
    g_autofree char* message =
      g_markup_printf_escaped("Cannot open %s: %s", job->name.c_str(), job->result.error.c_str());
    AdwToast* toast = adw_toast_new(message);
    adw_toast_set_timeout(toast, 8);
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(toasts_), toast);
    gtk_widget_set_visible(emptyPage_, !hasModel_);
  }
  else
  {
    const LoadTimings& timings = job->result.timings;
    std::printf("%s: parse %.1f ms | transfer %.1f ms | tessellate %.1f ms | prepare %.1f ms | "
                "%zu triangles, %zu line segments, %zu labels\n",
                job->name.c_str(), timings.parseMs, timings.transferMs, timings.meshMs,
                timings.buildMs, job->result.scene->triangleCount(), job->result.scene->edges.size(),
                job->result.scene->texts.size());
    if (!job->result.note.empty())
    {
      std::printf("%s: skipped unsupported entities: %s\n", job->name.c_str(),
                  job->result.note.c_str());
      AdwToast* toast = adw_toast_new("Some unsupported entities were skipped");
      adw_toast_set_timeout(toast, 6);
      adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(toasts_), toast);
    }
    std::fflush(stdout);

    modelSize_ = job->result.scene->size;
    modelUnit_ = job->result.scene->unit;
    flat_ = job->result.scene->flat;
    pendingScene_ = std::move(job->result.scene);
    currentName_ = job->name;
    hasModel_ = true;
    updateTitle();
    updateDimensions();
    applyModelKind();
    showScene();
  }

  if (!queued_.empty())
  {
    startLoad(std::exchange(queued_, {}));
  }
}

void ViewerWindow::showScene()
{
  if (!realized_ || !pendingScene_)
  {
    return;
  }
  gtk_gl_area_make_current(GTK_GL_AREA(area_));
  renderer_.setScene(*pendingScene_);
  if (pendingScene_->flat)
  {
    renderer_.setTexts(buildTextLayer(*pendingScene_));
  }
  camera_.setBounds(pendingScene_->boundsMin, pendingScene_->boundsMax);
  camera_.setPerspective(perspective_ && !flat_);
  camera_.lookFrom(flat_ ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, -1.0, 1.0});
  pendingScene_.reset();
  fitPending_ = true;
  redraw();
}

void ViewerWindow::updateTitle()
{
  const std::string title = currentName_.empty() ? kAppName : currentName_;
  adw_window_title_set_title(ADW_WINDOW_TITLE(title_), title.c_str());
  gtk_window_set_title(GTK_WINDOW(window_),
                       currentName_.empty() ? kAppName : (currentName_ + " — " + kAppName).c_str());
}

void ViewerWindow::applyModelKind()
{
  for (const char* name : {"style", "perspective", "cube", "view", "toggle-edges", "toggle-wireframe"})
  {
    g_simple_action_set_enabled(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(window_), name)),
                                !flat_);
  }
}

void ViewerWindow::updateDimensions()
{
  for (int axis = 0; axis < 3; ++axis)
  {
    const double value = modelSize_[axis];
    g_autofree char* number =
      value >= 10000.0 ? g_strdup_printf("%.0f", value) : g_strdup_printf("%.4g", value);
    const std::string text = modelUnit_.empty() ? std::string(number) : std::string(number) + " " + modelUnit_;
    gtk_label_set_text(GTK_LABEL(dimensionValues_[axis]), text.c_str());
    gtk_widget_set_visible(dimensionValues_[axis], axis < 2 || !flat_);
    gtk_widget_set_visible(dimensionNames_[axis], axis < 2 || !flat_);
  }
  gtk_widget_set_visible(dimensionsCard_, dimensions_ && hasModel_);
}

void ViewerWindow::uploadCubeLabels()
{
  constexpr int kTile = 160;
  const char* labels[6] = {"RIGHT", "LEFT", "BACK", "FRONT", "TOP", "BOTTOM"};

  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_A8, kTile * 6, kTile);
  cairo_t* cairo = cairo_create(surface);
  PangoLayout* layout = pango_cairo_create_layout(cairo);
  PangoFontDescription* font = pango_font_description_from_string("Sans Bold");
  pango_font_description_set_absolute_size(font, 34.0 * PANGO_SCALE);
  pango_layout_set_font_description(layout, font);
  for (int i = 0; i < 6; ++i)
  {
    int width = 0, height = 0;
    pango_layout_set_text(layout, labels[i], -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    cairo_move_to(cairo, i * kTile + (kTile - width) * 0.5, (kTile - height) * 0.5);
    pango_cairo_show_layout(cairo, layout);
  }
  pango_font_description_free(font);
  g_object_unref(layout);
  cairo_destroy(cairo);
  cairo_surface_flush(surface);

  const int stride = cairo_image_surface_get_stride(surface);
  const unsigned char* data = cairo_image_surface_get_data(surface);
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kTile) * 6 * kTile);
  for (int row = 0; row < kTile; ++row)
  {
    std::copy(data + row * stride, data + row * stride + kTile * 6,
              pixels.begin() + static_cast<std::ptrdiff_t>(row) * kTile * 6);
  }
  cairo_surface_destroy(surface);
  renderer_.setCubeLabels(pixels.data(), kTile);
}

void ViewerWindow::onRealize(GtkGLArea* area, gpointer self)
{
  ViewerWindow* viewer = from(self);
  gtk_gl_area_make_current(area);
  if (gtk_gl_area_get_error(area) != nullptr)
  {
    return;
  }
  try
  {
    viewer->renderer_.initialize();
  }
  catch (const std::exception& error)
  {
    g_autoptr(GError) failure = g_error_new(GDK_GL_ERROR, GDK_GL_ERROR_NOT_AVAILABLE, "%s", error.what());
    gtk_gl_area_set_error(area, failure);
    return;
  }
  viewer->uploadCubeLabels();
  viewer->realized_ = true;
  viewer->showScene();
}

void ViewerWindow::onUnrealize(GtkGLArea* area, gpointer self)
{
  ViewerWindow* viewer = from(self);
  if (!viewer->realized_)
  {
    return;
  }
  gtk_gl_area_make_current(area);
  viewer->renderer_.destroy();
  viewer->realized_ = false;
}

gboolean ViewerWindow::onRender(GtkGLArea* area, GdkGLContext*, gpointer self)
{
  ViewerWindow* viewer = from(self);
  if (!viewer->realized_)
  {
    return FALSE;
  }

  int viewport[4] = {};
  glGetIntegerv(GL_VIEWPORT, viewport);
  const double width = gtk_widget_get_width(GTK_WIDGET(area));
  const double height = gtk_widget_get_height(GTK_WIDGET(area));
  if (viewport[2] <= 0 || viewport[3] <= 0 || width <= 0.0 || height <= 0.0)
  {
    return TRUE;
  }

  viewer->camera_.setViewport(width, height);
  if (viewer->fitPending_)
  {
    viewer->camera_.fit();
    viewer->fitPending_ = false;
  }

  Renderer::Frame frame;
  frame.width = viewport[2];
  frame.height = viewport[3];
  frame.scale = static_cast<float>(viewport[3] / height);
  frame.style = viewer->style_;
  frame.cube = viewer->cube_;
  frame.flat = viewer->flat_;
  viewer->renderer_.draw(viewer->camera_, frame);
  return TRUE;
}

void ViewerWindow::onDragBegin(GtkGestureDrag* gesture, double x, double y, gpointer self)
{
  ViewerWindow* viewer = from(self);
  viewer->dragButton_ = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
  viewer->dragStartX_ = x;
  viewer->dragStartY_ = y;
  viewer->dragLastX_ = 0.0;
  viewer->dragLastY_ = 0.0;
}

void ViewerWindow::onDragUpdate(GtkGestureDrag*, double x, double y, gpointer self)
{
  ViewerWindow* viewer = from(self);
  const double dx = x - viewer->dragLastX_;
  const double dy = y - viewer->dragLastY_;
  viewer->dragLastX_ = x;
  viewer->dragLastY_ = y;
  if (!viewer->renderer_.hasScene())
  {
    return;
  }
  if (viewer->dragButton_ == GDK_BUTTON_PRIMARY && !viewer->flat_)
  {
    viewer->camera_.orbit(dx, dy);
  }
  else
  {
    viewer->camera_.pan(dx, dy);
  }
  viewer->redraw();
}

void ViewerWindow::onDragEnd(GtkGestureDrag*, double x, double y, gpointer self)
{
  ViewerWindow* viewer = from(self);
  const bool click = std::abs(x) < kClickSlop && std::abs(y) < kClickSlop;
  if (!click || viewer->dragButton_ != GDK_BUTTON_PRIMARY || !viewer->cube_ || viewer->flat_
      || !viewer->renderer_.hasScene())
  {
    return;
  }
  if (const std::optional<Vec3> direction =
        Renderer::pickCube(viewer->camera_, viewer->dragStartX_, viewer->dragStartY_))
  {
    viewer->camera_.lookFrom(*direction);
    viewer->camera_.fit();
    viewer->redraw();
  }
}

gboolean ViewerWindow::onScroll(GtkEventControllerScroll*, double, double dy, gpointer self)
{
  ViewerWindow* viewer = from(self);
  if (!viewer->renderer_.hasScene())
  {
    return FALSE;
  }
  viewer->camera_.zoomAt(viewer->cursorX_, viewer->cursorY_, std::pow(1.1, -dy));
  viewer->redraw();
  return TRUE;
}

void ViewerWindow::onMotion(GtkEventControllerMotion*, double x, double y, gpointer self)
{
  from(self)->cursorX_ = x;
  from(self)->cursorY_ = y;
}

void ViewerWindow::onZoomBegin(GtkGesture*, GdkEventSequence*, gpointer self)
{
  from(self)->zoomLast_ = 1.0;
}

void ViewerWindow::onZoomChanged(GtkGestureZoom* gesture, double scale, gpointer self)
{
  ViewerWindow* viewer = from(self);
  if (!viewer->renderer_.hasScene() || scale <= 0.0)
  {
    return;
  }
  double x = viewer->cursorX_, y = viewer->cursorY_;
  gtk_gesture_get_bounding_box_center(GTK_GESTURE(gesture), &x, &y);
  viewer->camera_.zoomAt(x, y, scale / viewer->zoomLast_);
  viewer->zoomLast_ = scale;
  viewer->redraw();
}

gboolean ViewerWindow::onDrop(GtkDropTarget*, const GValue* value, double, double, gpointer self)
{
  std::filesystem::path chosen;
  for (GSList* item = static_cast<GSList*>(g_value_get_boxed(value)); item != nullptr; item = item->next)
  {
    g_autofree char* name = g_file_get_path(G_FILE(item->data));
    if (name == nullptr)
    {
      continue;
    }
    if (chosen.empty() || (isSupportedFile(name) && !isSupportedFile(chosen)))
    {
      chosen = name;
    }
  }
  if (chosen.empty())
  {
    return FALSE;
  }
  from(self)->open(chosen);
  return TRUE;
}

gboolean ViewerWindow::onCloseRequest(GtkWindow* window, gpointer self)
{
  if (from(self)->job_ && adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(window)) == nullptr)
  {
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    std::fflush(nullptr);
    std::_Exit(EXIT_SUCCESS);
  }
  return FALSE;
}

gboolean ViewerWindow::onProgressTick(gpointer self)
{
  from(self)->updateProgress();
  return G_SOURCE_CONTINUE;
}

gboolean ViewerWindow::onLoadDone(gpointer self)
{
  from(self)->finishLoad();
  return G_SOURCE_REMOVE;
}

void ViewerWindow::onFileChosen(GObject* source, GAsyncResult* result, gpointer self)
{
  g_autoptr(GFile) file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, nullptr);
  if (file == nullptr)
  {
    return;
  }
  g_autofree char* name = g_file_get_path(file);
  if (name != nullptr)
  {
    from(self)->open(name);
  }
}

void ViewerWindow::actionOpen(GSimpleAction*, GVariant*, gpointer self)
{
  GtkFileFilter* step = gtk_file_filter_new();
  gtk_file_filter_set_name(step, "STEP and DXF files");
  gtk_file_filter_add_suffix(step, "step");
  gtk_file_filter_add_suffix(step, "stp");
  gtk_file_filter_add_suffix(step, "dxf");
  GtkFileFilter* all = gtk_file_filter_new();
  gtk_file_filter_set_name(all, "All files");
  gtk_file_filter_add_pattern(all, "*");
  g_autoptr(GListStore) filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  g_list_store_append(filters, step);
  g_list_store_append(filters, all);
  g_object_unref(step);
  g_object_unref(all);

  g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Open File");
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  gtk_file_dialog_open(dialog, from(self)->window(), nullptr, onFileChosen, self);
}

void ViewerWindow::actionClose(GSimpleAction*, GVariant*, gpointer self)
{
  gtk_window_close(from(self)->window());
}

void ViewerWindow::actionFit(GSimpleAction*, GVariant*, gpointer self)
{
  from(self)->camera_.fit();
  from(self)->redraw();
}

void ViewerWindow::actionView(GSimpleAction*, GVariant* parameter, gpointer self)
{
  const char* name = g_variant_get_string(parameter, nullptr);
  Vec3 direction{1.0, -1.0, 1.0};
  if (g_strcmp0(name, "front") == 0)
  {
    direction = {0.0, -1.0, 0.0};
  }
  else if (g_strcmp0(name, "right") == 0)
  {
    direction = {1.0, 0.0, 0.0};
  }
  else if (g_strcmp0(name, "top") == 0)
  {
    direction = {0.0, 0.0, 1.0};
  }
  from(self)->camera_.lookFrom(direction);
  from(self)->camera_.fit();
  from(self)->redraw();
}

void ViewerWindow::actionToggleEdges(GSimpleAction*, GVariant*, gpointer self)
{
  ViewerWindow* viewer = from(self);
  viewer->setStyle(viewer->style_ == Renderer::Style::ShadedEdges ? Renderer::Style::Shaded
                                                                   : Renderer::Style::ShadedEdges);
}

void ViewerWindow::actionToggleWireframe(GSimpleAction*, GVariant*, gpointer self)
{
  ViewerWindow* viewer = from(self);
  viewer->setStyle(viewer->style_ == Renderer::Style::Wireframe ? Renderer::Style::ShadedEdges
                                                                 : Renderer::Style::Wireframe);
}

void ViewerWindow::actionShortcuts(GSimpleAction*, GVariant*, gpointer self)
{
  struct Item
  {
    const char* title;
    const char* accelerator;
  };
  const auto section = [](const char* title, std::initializer_list<Item> items) {
    AdwShortcutsSection* result = adw_shortcuts_section_new(title);
    for (const Item& item : items)
    {
      adw_shortcuts_section_add(result, adw_shortcuts_item_new(item.title, item.accelerator));
    }
    return result;
  };

  AdwDialog* dialog = adw_shortcuts_dialog_new();
  adw_shortcuts_dialog_add(ADW_SHORTCUTS_DIALOG(dialog),
                           section("General", {{"Open a file", "<Control>o"},
                                               {"Keyboard shortcuts", "<Control>question"},
                                               {"Quit", "Escape <Control>q"}}));
  adw_shortcuts_dialog_add(ADW_SHORTCUTS_DIALOG(dialog),
                           section("View", {{"Fit to window", "f"},
                                            {"Toggle edges", "e"},
                                            {"Toggle wireframe", "w"},
                                            {"Toggle perspective", "p"},
                                            {"Toggle orientation cube", "c"},
                                            {"Toggle dimensions", "d"}}));
  adw_shortcuts_dialog_add(ADW_SHORTCUTS_DIALOG(dialog),
                           section("Standard Views", {{"Front", "1"},
                                                      {"Right", "2"},
                                                      {"Top", "3"},
                                                      {"Isometric", "0"}}));
  adw_dialog_present(dialog, GTK_WIDGET(from(self)->window_));
}

void ViewerWindow::actionAbout(GSimpleAction*, GVariant*, gpointer self)
{
  AdwDialog* dialog = adw_about_dialog_new();
  AdwAboutDialog* about = ADW_ABOUT_DIALOG(dialog);
  adw_about_dialog_set_application_name(about, kAppName);
  adw_about_dialog_set_application_icon(about, kAppId);
  adw_about_dialog_set_comments(about, "A STEP and DXF viewer for Linux.");
  adw_about_dialog_set_website(about, "https://github.com/ivanlukomskiy/your-sloppy-viewer");
  adw_about_dialog_set_license_type(about, GTK_LICENSE_MIT_X11);
  adw_dialog_present(dialog, GTK_WIDGET(from(self)->window_));
}

void ViewerWindow::changeStyle(GSimpleAction* action, GVariant* value, gpointer self)
{
  ViewerWindow* viewer = from(self);
  const char* name = g_variant_get_string(value, nullptr);
  viewer->style_ = g_strcmp0(name, "shaded") == 0      ? Renderer::Style::Shaded
                   : g_strcmp0(name, "wireframe") == 0 ? Renderer::Style::Wireframe
                                                       : Renderer::Style::ShadedEdges;
  g_simple_action_set_state(action, value);
  if (viewer->area_ != nullptr)
  {
    viewer->saveSettings();
    viewer->redraw();
  }
}

void ViewerWindow::changeCube(GSimpleAction* action, GVariant* value, gpointer self)
{
  ViewerWindow* viewer = from(self);
  viewer->cube_ = g_variant_get_boolean(value) != FALSE;
  g_simple_action_set_state(action, value);
  if (viewer->area_ != nullptr)
  {
    viewer->saveSettings();
    viewer->redraw();
  }
}

void ViewerWindow::changePerspective(GSimpleAction* action, GVariant* value, gpointer self)
{
  ViewerWindow* viewer = from(self);
  viewer->perspective_ = g_variant_get_boolean(value) != FALSE;
  viewer->camera_.setPerspective(viewer->perspective_ && !viewer->flat_);
  g_simple_action_set_state(action, value);
  if (viewer->area_ != nullptr)
  {
    viewer->saveSettings();
    viewer->redraw();
  }
}

void ViewerWindow::changeDimensions(GSimpleAction* action, GVariant* value, gpointer self)
{
  ViewerWindow* viewer = from(self);
  viewer->dimensions_ = g_variant_get_boolean(value) != FALSE;
  g_simple_action_set_state(action, value);
  if (viewer->area_ != nullptr)
  {
    viewer->saveSettings();
    viewer->updateDimensions();
  }
}
}

GtkWindow* createViewerWindow(AdwApplication* application, const std::filesystem::path& initialFile)
{
  return (new ViewerWindow(application, initialFile))->window();
}
