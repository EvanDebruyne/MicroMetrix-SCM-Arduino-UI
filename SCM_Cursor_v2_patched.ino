/* SCM_Cursor_v2_patched.ino
 * Arduino GIGA R1 + GIGA Display Shield
 * Simplified version with only menu bar and home page
 * Fixed sizing issues for proper full-screen layout
 */

#include "Arduino_GigaDisplay_GFX.h"
#include "Arduino_GigaDisplayTouch.h"
#include "lvgl.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

GigaDisplay_GFX gfx;
Arduino_GigaDisplayTouch Touch;

// --------------- Hardware Configuration -----------------
// Set to true to use real hardware inputs, false for mock data
#define USE_REAL_HARDWARE false

// Analog input pins for 4-20mA signals
// Note: For 4-20mA, you typically need a 250Ω resistor (or 165Ω for 3.3V max)
// to convert current to voltage: 4mA = 1V, 20mA = 5V (or 3.3V max with 165Ω)
#define PIN_4_20MA_CH1 A0   // Streaming Current or Particle Counter
#define PIN_4_20MA_CH2 A1   // Flow sensor
#define PIN_4_20MA_CH3 A2   // pH sensor
#define PIN_4_20MA_CH4 A3   // Temperature sensor

// 4-20mA scaling configuration
// For 250Ω resistor: 4mA = 1V, 20mA = 5V
// Arduino GIGA has 3.3V reference, so use 165Ω for full range: 4mA = 0.66V, 20mA = 3.3V
// Or use voltage divider if using 250Ω resistor
#define CURRENT_TO_VOLTAGE_RESISTOR 165.0f  // Ohms (165Ω for 3.3V max, 250Ω for 5V with divider)
#define ADC_RESOLUTION 4095.0f  // 12-bit ADC on GIGA R1
#define ADC_REFERENCE_VOLTAGE 3.3f  // Volts

// --------------- Data model (demo) -----------------
enum InputChannelIndex : uint8_t {
  CHANNEL_STREAMING_CURRENT = 0,
  CHANNEL_FLOW,
  CHANNEL_PH,
  CHANNEL_TEMPERATURE,
  INPUT_CHANNEL_COUNT
};

struct InputChannel {
  const char* id;
  const char* name;
  const char* unit;
  uint8_t decimalPlaces;
  float value;
  float minValue;
  float maxValue;
  float warnLow;
  float warnHigh;
  bool visible;
  bool editVisible;
  bool alarmActive;
  float calibrationOffset;
};

struct InputChannelUIBinding {
  lv_obj_t* card;
  lv_obj_t* valueLabel;
};

static lv_obj_t* inputs_status_labels[INPUT_CHANNEL_COUNT] = {};
static lv_obj_t* inputs_value_labels[INPUT_CHANNEL_COUNT] = {};

enum OutputChannelIndex : uint8_t {
  OUTPUT_COAGULANT_PUMP = 0,
  OUTPUT_POLYMER_PUMP,
  OUTPUT_ALARM_RELAY,
  OUTPUT_CHANNEL_COUNT
};

enum OutputChannelMode : uint8_t {
  OUTPUT_MODE_AUTO = 0,
  OUTPUT_MODE_MANUAL,
  OUTPUT_MODE_OFF
};

struct OutputChannel {
  const char* id;
  const char* name;
  const char* type;
  OutputChannelMode mode;
  bool enabled;
  float setpoint;
  float minSetpoint;
  float maxSetpoint;
  const char* unit;
};

struct OutputChannelUIBinding {
  lv_obj_t* card;
  lv_obj_t* modeLabel;
  lv_obj_t* statusLabel;
  lv_obj_t* setpointLabel;
  lv_obj_t* setpointSlider;
};

static InputChannel input_channels[INPUT_CHANNEL_COUNT] = {
  {
    "sc",
    "Streaming Current",
    "mV",
    1,
    0.0f,
    -3.0f,
    3.0f,
    -1.0f,
    1.0f,
    true,
    true,
    false,
    0.0f
  },
  {
    "flow",
    "Flow",
    "GPM",
    1,
    120.0f,
    0.0f,
    1000.0f,
    105.0f,
    130.0f,
    true,
    true,
    false,
    0.0f
  },
  {
    "ph",
    "pH",
    "",
    2,
    7.2f,
    0.0f,
    14.0f,
    7.05f,
    7.35f,
    true,
    true,
    false,
    0.0f
  },
  {
    "temp",
    "Temperature",
    "°C",
    1,
    22.5f,
    -20.0f,
    80.0f,
    21.8f,
    23.8f,
    true,
    true,
    false,
    0.0f
  }
};

static InputChannelUIBinding input_channel_ui[INPUT_CHANNEL_COUNT] = {};

static OutputChannel output_channels[OUTPUT_CHANNEL_COUNT] = {
  {
    "out_coag",
    "Coagulant Pump",
    "4-20mA",
    OUTPUT_MODE_AUTO,
    true,
    8.5f,
    0.0f,
    12.0f,
    "mA"
  },
  {
    "out_poly",
    "Polymer Pump",
    "Relay",
    OUTPUT_MODE_MANUAL,
    true,
    65.0f,
    0.0f,
    100.0f,
    "%"
  },
  {
    "out_alarm",
    "Process Alarm Relay",
    "Relay",
    OUTPUT_MODE_AUTO,
    false,
    1.0f,
    0.0f,
    1.0f,
    "state"
  }
};

static OutputChannelUIBinding output_channel_ui[OUTPUT_CHANNEL_COUNT] = {};

static const uint16_t TREND_HISTORY_LEN = 180;
static float trend_history[INPUT_CHANNEL_COUNT][TREND_HISTORY_LEN] = {};
static uint16_t trend_counts[INPUT_CHANNEL_COUNT] = {};

static const uint8_t MAX_ALARM_EVENTS = 16;
struct AlarmEvent {
  const char* channelId;
  const char* channelName;
  char message[64];
  bool active;
  uint32_t timestampMs;
};

static AlarmEvent alarm_events[MAX_ALARM_EVENTS] = {};
static uint8_t alarm_event_count = 0;
static bool previous_alarm_state[INPUT_CHANNEL_COUNT] = {};
static bool alarm_state_initialized = false;

static const float TREND_SCALE = 100.0f;
static const uint8_t GRAPH_Y_MAX_TICKS = 11;
static lv_obj_t* graphs_y_tick_labels[GRAPH_Y_MAX_TICKS] = {};
static lv_obj_t* graphs_y_label_container = nullptr;

// Placeholder for future alarm banner status
static bool alarm_banner_visible = false;

// Graph UI placeholders
static lv_obj_t* graphs_chart = nullptr;
static lv_obj_t* graphs_parameter_dropdown = nullptr;
static lv_chart_series_t* graphs_series = nullptr;

static lv_obj_t* home_alarm_log_label = nullptr;

// Forward declarations for UI helpers
static void show_input_detail_popup(InputChannel* channel);
static void populate_graphs_chart(uint8_t channel_index);
static void push_trend_sample(uint8_t channel_index, float value);
static void update_home_alarm_log();
static void record_alarm_event(InputChannel& channel, bool became_active);

// --------------- UI globals ------------------------
static lv_obj_t* root = nullptr;
static lv_obj_t* top = nullptr;
static lv_obj_t* content = nullptr;
static lv_obj_t* page_home = nullptr;
static lv_obj_t* page_inputs = nullptr;
static lv_obj_t* page_outputs = nullptr;
static lv_obj_t* page_graphs = nullptr;
static lv_obj_t* page_config = nullptr;

// Global reference to current layout editor popup
static lv_obj_t* current_layout_popup = nullptr;
static lv_obj_t* home_banner_container = nullptr;
static lv_obj_t* home_banner_label = nullptr;

// Splash screen state
static bool splash_active = true;
static lv_obj_t* splash_screen = nullptr;
static lv_timer_t* splash_timer = nullptr;

static lv_timer_t* ui_tick = nullptr;

// ----------- Splash Screen ------------------
static void on_loading_bar_anim(void* var, int32_t v) {
  lv_bar_set_value((lv_obj_t*)var, v, LV_ANIM_ON);
}

static void on_splash_timer(lv_timer_t* t) {
  splash_active = false;
  if (splash_screen && lv_obj_is_valid(splash_screen)) {
    lv_obj_del(splash_screen);
    splash_screen = nullptr;
  }
  lv_timer_del(splash_timer);
  splash_timer = nullptr;
  
  // Now build the main UI
  build_ui();
  ui_tick = lv_timer_create(ui_update_cb, 500, nullptr);
}

static void create_splash_screen() {
  splash_active = true;
  
  // Create full-screen splash screen
  splash_screen = lv_obj_create(lv_scr_act());
  lv_obj_set_size(splash_screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(splash_screen, 0, 0);
  lv_obj_set_style_bg_color(splash_screen, lv_color_hex(0x0066cc), 0); // Water blue background
  lv_obj_set_style_border_width(splash_screen, 0, 0);
  lv_obj_set_style_pad_all(splash_screen, 0, 0);
  
  // Large company name label (centered, will use default large font)
  lv_obj_t* company_label = lv_label_create(splash_screen);
  lv_label_set_text(company_label, "MicroMetrix");
  lv_obj_set_style_text_color(company_label, lv_color_hex(0xffffff), 0);
  lv_obj_align(company_label, LV_ALIGN_CENTER, 0, 0);
  
  // Set timer to close splash screen after 3 seconds
  splash_timer = lv_timer_create(on_splash_timer, 3000, nullptr);
}

// ----------- helpers ------------------
static lv_obj_t* make_card(lv_obj_t* parent) {
  // Safety check
  if (!parent || !lv_obj_is_valid(parent)) {
    return nullptr;
  }
  
  lv_obj_t* c = lv_obj_create(parent);
  if (!c) {
    return nullptr;
  }
  
  lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_radius(c, 16, 0);
  lv_obj_set_style_pad_all(c, 16, 0);
  lv_obj_set_style_bg_color(c, lv_color_hex(0x121218), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(c, lv_color_hex(0x2A2A34), 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

// --------------- event callbacks -------------------
static void show_page(lv_obj_t* page) {
  // Hide all pages first
  lv_obj_add_flag(page_home, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(page_inputs, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(page_outputs, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(page_graphs, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(page_config, LV_OBJ_FLAG_HIDDEN);
  
  // Show the selected page
  lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
}

static void on_nav_click(lv_event_t* e) {
  const char* id = (const char*)lv_event_get_user_data(e);
  if (!strcmp(id, "home")) show_page(page_home);
  if (!strcmp(id, "inputs")) show_page(page_inputs);
  if (!strcmp(id, "outputs")) show_page(page_outputs);
  if (!strcmp(id, "graphs")) show_page(page_graphs);
  if (!strcmp(id, "config")) show_page(page_config);
}

static void on_remove_channel(lv_event_t* e) {
  InputChannel* channel = static_cast<InputChannel*>(lv_event_get_user_data(e));
  if (!channel) return;
  channel->editVisible = false;
  refresh_layout_editor();
}

static void on_add_channel(lv_event_t* e) {
  InputChannel* channel = static_cast<InputChannel*>(lv_event_get_user_data(e));
  if (!channel) return;
  channel->editVisible = true;

  // Close the popup that triggered this event, if any
  lv_obj_t* btn = lv_event_get_target(e);
  if (btn) {
    lv_obj_t* popup = lv_obj_get_parent(btn);
    if (popup && lv_obj_is_valid(popup)) {
      lv_obj_del(popup);
    }
  }

  refresh_layout_editor();
}

static void on_close_popup(lv_event_t* e) {
  // Find and close the popup
  lv_obj_t* btn = lv_event_get_target(e);
  if (!btn) return;
  
  lv_obj_t* popup = lv_obj_get_parent(btn);
  while (popup && lv_obj_get_parent(popup) != content) {
    popup = lv_obj_get_parent(popup);
  }
  
  if (popup && lv_obj_is_valid(popup)) {
    // Clear the global reference if this was the layout editor popup
    if (popup == current_layout_popup) {
      current_layout_popup = nullptr;
    }
    lv_obj_del(popup);
  }
}

static void on_apply_changes(lv_event_t* e) {
  // Apply edit visibility to actual state
  for (size_t i = 0; i < INPUT_CHANNEL_COUNT; ++i) {
    input_channels[i].visible = input_channels[i].editVisible;
  }

  // Rebuild the home screen with the new layout
  rebuild_home_screen();

  // Refresh the preview so it reflects the applied state
  refresh_layout_editor();
  
  // Show a success message
  if (current_layout_popup && lv_obj_is_valid(current_layout_popup)) {
    lv_obj_t* success_label = lv_label_create(current_layout_popup);
    lv_label_set_text(success_label, "✓ Changes applied successfully!");
    lv_obj_set_style_text_color(success_label, lv_color_hex(0x10B981), 0);
    lv_obj_align(success_label, LV_ALIGN_TOP_MID, 0, 50);
  }
}

static void on_edit_home_layout(lv_event_t* e) {
  show_home_layout_editor();
}

static void on_show_display_settings(lv_event_t* e) {
  show_display_settings_popup();
}

static void on_show_add_container_menu(lv_event_t* e) {
  show_add_container_menu();
}

static void on_show_input_detail(lv_event_t* e) {
  InputChannel* channel = static_cast<InputChannel*>(lv_event_get_user_data(e));
  if (!channel) return;
  show_input_detail_popup(channel);
}

static void on_zero_calibration(lv_event_t* e) {
  InputChannel* channel = static_cast<InputChannel*>(lv_event_get_user_data(e));
  if (!channel) return;
  
  channel->calibrationOffset = channel->value;
  
  lv_obj_t* btn = lv_event_get_target(e);
  if (btn) {
    lv_obj_t* popup = lv_obj_get_parent(btn);
    while (popup && lv_obj_get_parent(popup) != content) {
      popup = lv_obj_get_parent(popup);
    }
    if (popup && lv_obj_is_valid(popup)) {
      lv_obj_del(popup);
    }
  }
}

static void on_output_mode_cycle(lv_event_t* e) {
  OutputChannel* channel = static_cast<OutputChannel*>(lv_event_get_user_data(e));
  if (!channel) return;
  ptrdiff_t index = channel - output_channels;
  if (index < 0 || index >= OUTPUT_CHANNEL_COUNT) return;

  channel->mode = static_cast<OutputChannelMode>((channel->mode + 1) % 3);
  update_output_card_ui(static_cast<size_t>(index));
}

static void on_output_toggle_enable(lv_event_t* e) {
  OutputChannel* channel = static_cast<OutputChannel*>(lv_event_get_user_data(e));
  if (!channel) return;
  ptrdiff_t index = channel - output_channels;
  if (index < 0 || index >= OUTPUT_CHANNEL_COUNT) return;

  channel->enabled = !channel->enabled;
  update_output_card_ui(static_cast<size_t>(index));
}

static void on_output_setpoint_changed(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

  OutputChannel* channel = static_cast<OutputChannel*>(lv_event_get_user_data(e));
  if (!channel) return;
  ptrdiff_t index = channel - output_channels;
  if (index < 0 || index >= OUTPUT_CHANNEL_COUNT) return;

  lv_obj_t* slider = lv_event_get_target(e);
  if (!slider || !lv_obj_is_valid(slider)) return;

  int32_t slider_value = lv_slider_get_value(slider);
  channel->setpoint = slider_value / 10.0f;
  update_output_card_ui(static_cast<size_t>(index));
}

static void on_graph_parameter_changed(lv_event_t* e) {
  LV_UNUSED(e);
  if (!graphs_parameter_dropdown || !lv_obj_is_valid(graphs_parameter_dropdown)) return;
  uint16_t selected = lv_dropdown_get_selected(graphs_parameter_dropdown);
  if (selected >= INPUT_CHANNEL_COUNT) selected = 0;
  populate_graphs_chart(static_cast<uint8_t>(selected));
}

// Cleanup function to ensure all popups are closed
static void cleanup_all_popups() {
  // Clear the layout editor reference
  if (current_layout_popup) {
    current_layout_popup = nullptr;
  }
  
  // Close any remaining popups
  for (int i = lv_obj_get_child_cnt(content) - 1; i >= 0; i--) {
    lv_obj_t* child = lv_obj_get_child(content, i);
    if (child && lv_obj_is_valid(child)) {
      lv_obj_del(child);
    }
  }
}

static void format_channel_value(const InputChannel& channel, char* buffer, size_t len) {
  if (!buffer || len == 0) return;

  float displayed_value = channel.value - channel.calibrationOffset;

  char value_fmt[8];
  snprintf(value_fmt, sizeof(value_fmt), "%%.%df", channel.decimalPlaces);

  char value_only[32];
  snprintf(value_only, sizeof(value_only), value_fmt, displayed_value);

  if (channel.unit && channel.unit[0] != '\0') {
    snprintf(buffer, len, "%s %s", value_only, channel.unit);
  } else {
    snprintf(buffer, len, "%s", value_only);
  }
}

static const char* output_mode_to_string(OutputChannelMode mode) {
  switch (mode) {
    case OUTPUT_MODE_AUTO: return "Auto";
    case OUTPUT_MODE_MANUAL: return "Manual";
    case OUTPUT_MODE_OFF: return "Off";
    default: return "Unknown";
  }
}

static void format_output_setpoint(const OutputChannel& channel, char* buffer, size_t len) {
  if (!buffer || len == 0) return;

  if (strcmp(channel.unit, "state") == 0) {
    snprintf(buffer, len, "%s", channel.enabled ? "Energized" : "Idle");
    return;
  }

  snprintf(buffer, len, "%.1f %s", channel.setpoint, channel.unit);
}

static void update_output_card_ui(size_t index) {
  if (index >= OUTPUT_CHANNEL_COUNT) return;
  OutputChannel& channel = output_channels[index];
  OutputChannelUIBinding& binding = output_channel_ui[index];

  if (binding.modeLabel && lv_obj_is_valid(binding.modeLabel)) {
    lv_label_set_text(binding.modeLabel, output_mode_to_string(channel.mode));
  }

  if (binding.statusLabel && lv_obj_is_valid(binding.statusLabel)) {
    lv_label_set_text(binding.statusLabel, channel.enabled ? "Enabled" : "Disabled");
    lv_obj_set_style_text_color(
      binding.statusLabel,
      channel.enabled ? lv_color_hex(0x34D399) : lv_color_hex(0xF87171),
      0
    );
  }

  if (binding.setpointLabel && lv_obj_is_valid(binding.setpointLabel)) {
    char buf[32];
    format_output_setpoint(channel, buf, sizeof(buf));
    lv_label_set_text(binding.setpointLabel, buf);
  }

  if (binding.setpointSlider && lv_obj_is_valid(binding.setpointSlider)) {
    int32_t slider_val = static_cast<int32_t>(channel.setpoint * 10.0f);
    lv_slider_set_value(binding.setpointSlider, slider_val, LV_ANIM_OFF);
  }
}

static void update_home_banner() {
  if (!home_banner_container || !lv_obj_is_valid(home_banner_container)) return;
  if (!home_banner_label || !lv_obj_is_valid(home_banner_label)) return;

  if (alarm_banner_visible) {
    lv_obj_clear_flag(home_banner_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(home_banner_container, lv_color_hex(0x7F1D1D), 0);
    lv_label_set_text(home_banner_label, "Active Alarms - Review Immediately");
    lv_obj_set_style_text_color(home_banner_label, lv_color_hex(0xFCA5A5), 0);
  } else {
    lv_obj_set_style_bg_color(home_banner_container, lv_color_hex(0x14532D), 0);
    lv_label_set_text(home_banner_label, "System Normal - All Channels Within Limits");
    lv_obj_set_style_text_color(home_banner_label, lv_color_hex(0xBBF7D0), 0);
  }
}

static void push_trend_sample(uint8_t channel_index, float value) {
  if (channel_index >= INPUT_CHANNEL_COUNT) return;

  uint16_t count = trend_counts[channel_index];
  if (count < TREND_HISTORY_LEN) {
    trend_history[channel_index][count] = value;
    trend_counts[channel_index] = count + 1;
  } else {
    memmove(
      trend_history[channel_index],
      trend_history[channel_index] + 1,
      (TREND_HISTORY_LEN - 1) * sizeof(float)
    );
    trend_history[channel_index][TREND_HISTORY_LEN - 1] = value;
    trend_counts[channel_index] = TREND_HISTORY_LEN;
  }
}

static void record_alarm_event(InputChannel& channel, bool became_active) {
  if (alarm_event_count >= MAX_ALARM_EVENTS) {
    memmove(alarm_events, alarm_events + 1, (MAX_ALARM_EVENTS - 1) * sizeof(AlarmEvent));
    alarm_event_count = MAX_ALARM_EVENTS - 1;
  }

  AlarmEvent& evt = alarm_events[alarm_event_count];
  evt.channelId = channel.id;
  evt.channelName = channel.name;
  snprintf(evt.message, sizeof(evt.message), "%s %s",
           channel.name,
           became_active ? "alarm active" : "returned to normal");
  evt.active = became_active;
  evt.timestampMs = millis();

  alarm_event_count++;
  update_home_alarm_log();
}

static void update_home_alarm_log() {
  if (!home_alarm_log_label || !lv_obj_is_valid(home_alarm_log_label)) {
    return;
  }

  if (alarm_event_count == 0) {
    lv_label_set_text(home_alarm_log_label, "No alarm events logged.");
    return;
  }

  uint32_t current_time = millis();
  char buf[256];
  buf[0] = '\0';
  
  uint8_t events_to_show = (alarm_event_count < 4) ? alarm_event_count : 4;
  uint8_t start_index = (alarm_event_count >= events_to_show) ? (alarm_event_count - events_to_show) : 0;
  
  for (uint8_t i = start_index; i < alarm_event_count; ++i) {
    const AlarmEvent& evt = alarm_events[i];
    uint32_t elapsed_ms = current_time - evt.timestampMs;
    uint32_t elapsed_seconds = elapsed_ms / 1000UL;
    uint32_t elapsed_minutes = elapsed_seconds / 60UL;
    elapsed_seconds %= 60UL;
    
    char time_str[32];
    if (elapsed_minutes > 0) {
      snprintf(time_str, sizeof(time_str), "%lum ago", elapsed_minutes);
    } else if (elapsed_seconds > 0) {
      snprintf(time_str, sizeof(time_str), "%lus ago", elapsed_seconds);
    } else {
      snprintf(time_str, sizeof(time_str), "now");
    }
    
    char line[96];
    snprintf(line, sizeof(line), "%s - %s\n", evt.message, time_str);
    
    if (strlen(buf) + strlen(line) < sizeof(buf) - 1) {
      strcat(buf, line);
    } else {
      break;
    }
  }
  
  if (strlen(buf) > 0) {
    buf[strlen(buf) - 1] = '\0';
  }
  
  lv_label_set_text(home_alarm_log_label, buf);
}

static void populate_graphs_chart(uint8_t channel_index) {
  if (channel_index >= INPUT_CHANNEL_COUNT) channel_index = 0;
  if (!graphs_chart || !lv_obj_is_valid(graphs_chart)) return;

  const InputChannel& channel = input_channels[channel_index];

  if (!graphs_series) {
    graphs_series = lv_chart_add_series(graphs_chart, lv_color_hex(0x3B82F6), LV_CHART_AXIS_PRIMARY_Y);
  }

  lv_chart_set_all_value(graphs_chart, graphs_series, 0);

  float amplitude = (channel.warnHigh - channel.warnLow) * 0.2f;
  if (amplitude < 0.1f) {
    amplitude = (channel.maxValue - channel.minValue) * 0.1f;
  }
  if (amplitude < 0.1f) {
    amplitude = 1.0f;
  }

  float axis_min = channel.minValue;
  float axis_max = channel.maxValue;
  uint8_t tick_count = 6;
  float tick_step = 1.0f;
  bool custom_axis = false;

  if (!strcmp(channel.id, "sc")) {
    axis_min = -3.0f;
    axis_max = 3.0f;
    tick_count = 7;
    tick_step = 1.0f;
    custom_axis = true;
  } else if (!strcmp(channel.id, "flow")) {
    axis_min = 0.0f;
    axis_max = 250.0f;
    tick_count = 6;
    tick_step = 50.0f;
    custom_axis = true;
  } else if (!strcmp(channel.id, "ph")) {
    axis_min = 0.0f;
    axis_max = 10.0f;
    tick_count = 11;
    tick_step = 1.0f;
    custom_axis = true;
  } else if (!strcmp(channel.id, "temp")) {
    axis_min = 0.0f;
    axis_max = 40.0f;
    tick_count = 9;
    tick_step = 5.0f;
    custom_axis = true;
  }

  if (!custom_axis) {
    float warn_span = channel.warnHigh - channel.warnLow;
    if (warn_span < 0.01f) {
      warn_span = (channel.maxValue - channel.minValue) * 0.1f;
      if (warn_span < 1.0f) warn_span = 1.0f;
    }
    axis_min = channel.warnLow - warn_span * 0.6f;
    axis_max = channel.warnHigh + warn_span * 0.6f;
    if (axis_min > channel.value) axis_min = channel.value - warn_span * 0.6f;
    if (axis_max < channel.value) axis_max = channel.value + warn_span * 0.6f;

    if (axis_min < channel.minValue) axis_min = channel.minValue;
    if (axis_max > channel.maxValue) axis_max = channel.maxValue;
    if (axis_max - axis_min < warn_span * 0.4f) axis_max = axis_min + warn_span * 0.4f;
    if (axis_max <= axis_min) axis_max = axis_min + 1.0f;

    tick_count = 6;
    tick_step = (axis_max - axis_min) / (tick_count - 1);
  }

  int32_t scaled_min = static_cast<int32_t>(floorf(axis_min * TREND_SCALE));
  int32_t scaled_max = static_cast<int32_t>(ceilf(axis_max * TREND_SCALE));

  lv_chart_set_range(graphs_chart, LV_CHART_AXIS_PRIMARY_Y, scaled_min, scaled_max);
  lv_chart_set_axis_tick(graphs_chart, LV_CHART_AXIS_PRIMARY_Y, 8, 4, tick_count, 0, true, 50);

  float step = (tick_count > 1) ? tick_step : 0.0f;
  float value = axis_max;
  for (uint8_t i = 0; i < GRAPH_Y_MAX_TICKS; ++i) {
    if (!graphs_y_tick_labels[i] || !lv_obj_is_valid(graphs_y_tick_labels[i])) continue;
    if (i < tick_count) {
      char tick[20];
      if (fabsf(value) >= 100.0f) {
        snprintf(tick, sizeof(tick), "%.0f", value);
      } else if (fabsf(value) >= 10.0f) {
        snprintf(tick, sizeof(tick), "%.1f", value);
      } else {
        snprintf(tick, sizeof(tick), "%.2f", value);
      }
      lv_label_set_text(graphs_y_tick_labels[i], tick);
      lv_obj_clear_flag(graphs_y_tick_labels[i], LV_OBJ_FLAG_HIDDEN);
      value -= step;
    } else {
      lv_obj_add_flag(graphs_y_tick_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  uint16_t point_count = lv_chart_get_point_count(graphs_chart);
  uint16_t available = trend_counts[channel_index];

  if (available == 0) {
    for (uint16_t i = 0; i < point_count; ++i) {
      float clamped = channel.value;
      if (clamped < axis_min) clamped = axis_min;
      if (clamped > axis_max) clamped = axis_max;
      int16_t chart_value = static_cast<int16_t>(clamped * TREND_SCALE);
      lv_chart_set_value_by_id(graphs_chart, graphs_series, i, chart_value);
    }

    lv_chart_set_axis_tick(graphs_chart, LV_CHART_AXIS_PRIMARY_X, 0, 0, 0, 0, false, 0);
    lv_chart_refresh(graphs_chart);
    return;
  }

  uint16_t start_index = 0;
  if (available > point_count) {
    start_index = available - point_count;
  }

  for (uint16_t i = 0; i < point_count; ++i) {
    uint16_t history_index = start_index + i;
    float sample_value;
    if (history_index < available) {
      sample_value = trend_history[channel_index][history_index];
    } else {
      sample_value = trend_history[channel_index][available - 1];
    }
    if (sample_value < axis_min) sample_value = axis_min;
    if (sample_value > axis_max) sample_value = axis_max;
    int16_t chart_value = static_cast<int16_t>(sample_value * TREND_SCALE);
    lv_chart_set_value_by_id(graphs_chart, graphs_series, i, chart_value);
  }

  lv_chart_set_axis_tick(graphs_chart, LV_CHART_AXIS_PRIMARY_X, 0, 0, 0, 0, false, 0);

  lv_chart_refresh(graphs_chart);
}

static void refresh_layout_editor() {
  // Use the global reference to the current layout editor popup
  if (current_layout_popup && lv_obj_is_valid(current_layout_popup)) {
    // Find the preview container within the popup
    lv_obj_t* preview_container = nullptr;
    for (int i = 0; i < lv_obj_get_child_cnt(current_layout_popup); i++) {
      lv_obj_t* child = lv_obj_get_child(current_layout_popup, i);
      if (child && lv_obj_is_valid(child) && lv_obj_get_user_data(child) == (void*)0x1234) {
        preview_container = child;
        break;
      }
    }
    
    if (preview_container && lv_obj_is_valid(preview_container)) {
      // Clear and rebuild the preview
      lv_obj_clean(preview_container);
      build_layout_preview(preview_container);
    }
  }
}

// --------------- page builders ---------------------
static void build_topbar() {
  top = lv_obj_create(root);
  lv_obj_set_size(top, LV_PCT(100), 64);
  lv_obj_set_style_bg_color(top, lv_color_hex(0x0D0D12), 0);
  lv_obj_set_style_border_width(top, 0, 0);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(top, 8, 0);
  lv_obj_set_style_pad_gap(top, 8, 0);

  auto make_tab = [&](const char* name, const char* id){
    lv_obj_t* btn = lv_btn_create(top);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1B1B22), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1D4ED8), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* lab = lv_label_create(btn);
    lv_label_set_text(lab, name);
    lv_obj_center(lab);
    lv_obj_add_event_cb(btn, on_nav_click, LV_EVENT_CLICKED, (void*)id);
    return btn;
  };

  make_tab("Home", "home");
  make_tab("Inputs", "inputs");
  make_tab("Outputs", "outputs");
  make_tab("Graphs", "graphs");
  make_tab("Config", "config");
}

static void build_home_content(lv_obj_t* page) {
  // Safety check
  if (!page || !lv_obj_is_valid(page)) {
    return;
  }
  
  home_alarm_log_label = nullptr;
  lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(page, 20, 0);
  lv_obj_set_style_pad_row(page, 20, 0);
  // Enable vertical scrolling for home page
  lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(page, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);

  // Alarm banner region
  home_banner_container = lv_obj_create(page);
  lv_obj_set_size(home_banner_container, LV_PCT(100), 80);
  lv_obj_set_style_pad_all(home_banner_container, 20, 0);
  lv_obj_set_style_radius(home_banner_container, 14, 0);
  lv_obj_set_style_border_width(home_banner_container, 0, 0);
  lv_obj_clear_flag(home_banner_container, LV_OBJ_FLAG_SCROLLABLE);

  home_banner_label = lv_label_create(home_banner_container);
  lv_label_set_text(home_banner_label, "System Normal - All Channels Within Limits");
  lv_obj_set_style_text_color(home_banner_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(home_banner_label);

  // Create a main container that can expand beyond viewport for scrolling
  lv_obj_t* main_container = lv_obj_create(page);
  lv_obj_set_size(main_container, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(main_container, 20, 0);
  lv_obj_set_style_pad_row(main_container, 20, 0);
  lv_obj_set_style_bg_opa(main_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(main_container, 0, 0);
  lv_obj_clear_flag(main_container, LV_OBJ_FLAG_SCROLLABLE);

  // Clear existing UI bindings
  for (size_t i = 0; i < INPUT_CHANNEL_COUNT; ++i) {
    input_channel_ui[i].card = nullptr;
    input_channel_ui[i].valueLabel = nullptr;
  }

  // Create data cards based on visibility settings
  for (size_t i = 0; i < INPUT_CHANNEL_COUNT; ++i) {
    InputChannel& channel = input_channels[i];
    if (!channel.visible) continue;

    lv_obj_t* card = make_card(main_container);
    lv_obj_set_size(card, LV_PCT(100), 140);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 25, 0);
    lv_obj_set_style_pad_row(card, 14, 0);

    char title_buf[48];
    if (channel.unit && channel.unit[0] != '\0') {
      snprintf(title_buf, sizeof(title_buf), "%s (%s)", channel.name, channel.unit);
    } else {
      snprintf(title_buf, sizeof(title_buf), "%s", channel.name);
    }

    lv_obj_t* title_lbl = lv_label_create(card);
    lv_label_set_text(title_lbl, title_buf);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x9CA3AF), 0);

    char value_buf[48];
    format_channel_value(channel, value_buf, sizeof(value_buf));

    lv_obj_t* value_lbl = lv_label_create(card);
    lv_label_set_text(value_lbl, value_buf);
    lv_obj_set_style_text_color(value_lbl, lv_color_hex(0xFFFFFF), 0);

    input_channel_ui[i].card = card;
    input_channel_ui[i].valueLabel = value_lbl;
  }

  lv_obj_t* alarm_card = make_card(main_container);
  lv_obj_set_size(alarm_card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(alarm_card, 120, 0);
  lv_obj_set_flex_flow(alarm_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(alarm_card, 10, 0);
  lv_obj_set_style_pad_all(alarm_card, 20, 0);

  lv_obj_t* alarm_title = lv_label_create(alarm_card);
  lv_label_set_text(alarm_title, "Alarm Activity");
  lv_obj_set_style_text_color(alarm_title, lv_color_hex(0x9CA3AF), 0);

  home_alarm_log_label = lv_label_create(alarm_card);
  lv_obj_set_size(home_alarm_log_label, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_text_color(home_alarm_log_label, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_long_mode(home_alarm_log_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(home_alarm_log_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_line_space(home_alarm_log_label, 4, 0);
  lv_label_set_text(home_alarm_log_label, "No alarm events logged.");

  update_home_alarm_log();
  update_home_banner();
}

static void build_inputs_content(lv_obj_t* page) {
  lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(page, 20, 0);
  lv_obj_set_style_pad_row(page, 20, 0);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* main_container = lv_obj_create(page);
  lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(main_container, 20, 0);
  lv_obj_set_style_pad_row(main_container, 20, 0);
  lv_obj_set_style_bg_opa(main_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(main_container, 0, 0);
  lv_obj_clear_flag(main_container, LV_OBJ_FLAG_SCROLLABLE);

  // Page title
  lv_obj_t* title = lv_label_create(main_container);
  lv_label_set_text(title, "Inputs Configuration");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  // Use default font - no custom font setting needed

  lv_obj_t* list_container = lv_obj_create(main_container);
  lv_obj_set_size(list_container, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(list_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list_container, 0, 0);
  lv_obj_set_style_pad_row(list_container, 18, 0);
  lv_obj_set_style_pad_column(list_container, 0, 0);
  lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(list_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(list_container, 2, 0);

  for (size_t i = 0; i < INPUT_CHANNEL_COUNT; ++i) {
    inputs_status_labels[i] = nullptr;
    inputs_value_labels[i] = nullptr;
  }

  for (size_t i = 0; i < INPUT_CHANNEL_COUNT; ++i) {
    InputChannel& channel = input_channels[i];
    lv_obj_t* card = make_card(list_container);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_style_pad_column(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* header = lv_obj_create(card);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_column(header, 10, 0);

    lv_obj_t* name_lbl = lv_label_create(header);
    lv_label_set_text(name_lbl, channel.name);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t* status_lbl = lv_label_create(header);
    inputs_status_labels[i] = status_lbl;
    lv_obj_align(status_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_label_set_text(status_lbl, channel.alarmActive ? "Alarm" : "Normal");
    lv_obj_set_style_text_color(status_lbl, channel.alarmActive ? lv_color_hex(0xF87171) : lv_color_hex(0x34D399), 0);

    char value_buf[48];
    format_channel_value(channel, value_buf, sizeof(value_buf));

    lv_obj_t* value_lbl = lv_label_create(card);
    inputs_value_labels[i] = value_lbl;
    lv_label_set_text(value_lbl, value_buf);
    lv_obj_set_style_text_color(value_lbl, lv_color_hex(0xE5E7EB), 0);

    char range_buf[96];
    snprintf(range_buf, sizeof(range_buf), "Range: %.1f to %.1f %s\nAlarm Window: %.1f to %.1f %s",
             channel.minValue,
             channel.maxValue,
             channel.unit,
             channel.warnLow,
             channel.warnHigh,
             channel.unit);

    lv_obj_t* range_lbl = lv_label_create(card);
    lv_label_set_text(range_lbl, range_buf);
    lv_obj_set_style_text_color(range_lbl, lv_color_hex(0x9CA3AF), 0);

    lv_obj_t* detail_btn = lv_btn_create(card);
    lv_obj_set_size(detail_btn, 140, 42);
    lv_obj_set_style_bg_color(detail_btn, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_radius(detail_btn, 10, 0);

    lv_obj_t* detail_lbl = lv_label_create(detail_btn);
    lv_label_set_text(detail_lbl, "View Details");
    lv_obj_center(detail_lbl);

    lv_obj_add_event_cb(detail_btn, on_show_input_detail, LV_EVENT_CLICKED, &channel);
  }
}

static void build_outputs_content(lv_obj_t* page) {
  lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(page, 20, 0);
  lv_obj_set_style_pad_row(page, 20, 0);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* main_container = lv_obj_create(page);
  lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(main_container, 20, 0);
  lv_obj_set_style_pad_row(main_container, 20, 0);
  lv_obj_set_style_bg_opa(main_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(main_container, 0, 0);
  lv_obj_clear_flag(main_container, LV_OBJ_FLAG_SCROLLABLE);

  // Page title
  lv_obj_t* title = lv_label_create(main_container);
  lv_label_set_text(title, "Outputs Control");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  // Use default font - no custom font setting needed
  
  lv_obj_t* list_container = lv_obj_create(main_container);
  lv_obj_set_size(list_container, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(list_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list_container, 0, 0);
  lv_obj_set_style_pad_row(list_container, 18, 0);
  lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(list_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(list_container, 2, 0);

  for (size_t i = 0; i < OUTPUT_CHANNEL_COUNT; ++i) {
    OutputChannel& channel = output_channels[i];
    OutputChannelUIBinding& binding = output_channel_ui[i];
    binding = {};

    lv_obj_t* card = make_card(list_container);
    binding.card = card;
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* header = lv_obj_create(card);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_column(header, 12, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);

    lv_obj_t* name_lbl = lv_label_create(header);
    lv_label_set_text(name_lbl, channel.name);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t* type_lbl = lv_label_create(header);
    lv_label_set_text_fmt(type_lbl, "%s Output", channel.type);
    lv_obj_set_style_text_color(type_lbl, lv_color_hex(0x9CA3AF), 0);

    lv_obj_t* mode_row = lv_obj_create(card);
    lv_obj_set_size(mode_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(mode_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mode_row, 0, 0);
    lv_obj_set_style_pad_all(mode_row, 0, 0);
    lv_obj_set_style_pad_column(mode_row, 12, 0);
    lv_obj_set_flex_flow(mode_row, LV_FLEX_FLOW_ROW);

    lv_obj_t* mode_caption = lv_label_create(mode_row);
    lv_label_set_text(mode_caption, "Mode");
    lv_obj_set_style_text_color(mode_caption, lv_color_hex(0x9CA3AF), 0);

    binding.modeLabel = lv_label_create(mode_row);
    lv_obj_set_style_text_color(binding.modeLabel, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t* mode_btn = lv_btn_create(mode_row);
    lv_obj_set_size(mode_btn, 120, 40);
    lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0x6366F1), 0);
    lv_obj_set_style_radius(mode_btn, 10, 0);
    lv_obj_add_event_cb(mode_btn, on_output_mode_cycle, LV_EVENT_CLICKED, &channel);

    lv_obj_t* mode_btn_lbl = lv_label_create(mode_btn);
    lv_label_set_text(mode_btn_lbl, "Change");
    lv_obj_center(mode_btn_lbl);

    lv_obj_t* status_row = lv_obj_create(card);
    lv_obj_set_size(status_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_row, 0, 0);
    lv_obj_set_style_pad_all(status_row, 0, 0);
    lv_obj_set_style_pad_column(status_row, 12, 0);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);

    lv_obj_t* status_caption = lv_label_create(status_row);
    lv_label_set_text(status_caption, "Status");
    lv_obj_set_style_text_color(status_caption, lv_color_hex(0x9CA3AF), 0);

    binding.statusLabel = lv_label_create(status_row);

    lv_obj_t* enable_btn = lv_btn_create(status_row);
    lv_obj_set_size(enable_btn, 140, 40);
    lv_obj_set_style_bg_color(enable_btn, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_radius(enable_btn, 10, 0);
    lv_obj_add_event_cb(enable_btn, on_output_toggle_enable, LV_EVENT_CLICKED, &channel);

    lv_obj_t* enable_lbl = lv_label_create(enable_btn);
    lv_label_set_text(enable_lbl, "Toggle Enable");
    lv_obj_center(enable_lbl);

    lv_obj_t* setpoint_row = lv_obj_create(card);
    lv_obj_set_size(setpoint_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(setpoint_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(setpoint_row, 0, 0);
    lv_obj_set_style_pad_all(setpoint_row, 0, 0);
    lv_obj_set_style_pad_column(setpoint_row, 12, 0);
    lv_obj_set_flex_flow(setpoint_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_row(setpoint_row, 0, 0);

    lv_obj_t* setpoint_caption = lv_label_create(setpoint_row);
    lv_label_set_text(setpoint_caption, "Setpoint");
    lv_obj_set_style_text_color(setpoint_caption, lv_color_hex(0x9CA3AF), 0);

    binding.setpointLabel = lv_label_create(setpoint_row);
    lv_obj_set_style_text_color(binding.setpointLabel, lv_color_hex(0xFFFFFF), 0);

    if (strcmp(channel.unit, "state") != 0) {
      lv_obj_t* slider = lv_slider_create(card);
      binding.setpointSlider = slider;
      lv_obj_set_size(slider, LV_PCT(100), 12);
      lv_slider_set_range(slider, static_cast<int32_t>(channel.minSetpoint * 10.0f), static_cast<int32_t>(channel.maxSetpoint * 10.0f));
      lv_slider_set_value(slider, static_cast<int32_t>(channel.setpoint * 10.0f), LV_ANIM_OFF);
      lv_obj_add_event_cb(slider, on_output_setpoint_changed, LV_EVENT_VALUE_CHANGED, &channel);
    } else {
      binding.setpointSlider = nullptr;
    }

    update_output_card_ui(i);
  }
}

static void build_graphs_content(lv_obj_t* page) {
  lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(page, 20, 0);
  lv_obj_set_style_pad_row(page, 20, 0);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* main_container = lv_obj_create(page);
  lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(main_container, 20, 0);
  lv_obj_set_style_pad_row(main_container, 20, 0);
  lv_obj_set_style_bg_opa(main_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(main_container, 0, 0);
  lv_obj_clear_flag(main_container, LV_OBJ_FLAG_SCROLLABLE);

  // Page title
  lv_obj_t* title = lv_label_create(main_container);
  lv_label_set_text(title, "Data Graphs");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  // Use default font - no custom font setting needed

  lv_obj_t* controls_row = lv_obj_create(main_container);
  lv_obj_set_size(controls_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(controls_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(controls_row, 0, 0);
  lv_obj_set_style_pad_all(controls_row, 0, 0);
  lv_obj_clear_flag(controls_row, LV_OBJ_FLAG_SCROLLABLE);

  graphs_parameter_dropdown = lv_dropdown_create(controls_row);
  lv_obj_set_width(graphs_parameter_dropdown, 250);
  lv_dropdown_set_options_static(graphs_parameter_dropdown, "Streaming Current\nFlow\npH\nTemperature");
  lv_obj_add_event_cb(graphs_parameter_dropdown, on_graph_parameter_changed, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t* chart_wrapper = lv_obj_create(main_container);
  lv_obj_set_size(chart_wrapper, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(chart_wrapper, 1);
  lv_obj_set_style_bg_opa(chart_wrapper, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chart_wrapper, 0, 0);
  lv_obj_set_style_pad_all(chart_wrapper, 0, 0);
  lv_obj_set_style_pad_column(chart_wrapper, 6, 0);
  lv_obj_set_flex_flow(chart_wrapper, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(chart_wrapper, LV_OBJ_FLAG_SCROLLABLE);

  graphs_y_label_container = lv_obj_create(chart_wrapper);
  lv_obj_set_size(graphs_y_label_container, 50, LV_PCT(100));
  lv_obj_set_style_bg_opa(graphs_y_label_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(graphs_y_label_container, 0, 0);
  lv_obj_set_style_pad_all(graphs_y_label_container, 0, 0);
  lv_obj_set_style_pad_row(graphs_y_label_container, 6, 0);
  lv_obj_set_flex_flow(graphs_y_label_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(graphs_y_label_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

  for (uint8_t i = 0; i < GRAPH_Y_MAX_TICKS; ++i) {
    lv_obj_t* lbl = lv_label_create(graphs_y_label_container);
    lv_label_set_text(lbl, "--");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xA5B4FC), 0);
    graphs_y_tick_labels[i] = lbl;
  }

  graphs_chart = lv_chart_create(chart_wrapper);
  lv_obj_set_size(graphs_chart, LV_PCT(100), LV_PCT(100));
  lv_chart_set_type(graphs_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(graphs_chart, 60);
  lv_chart_set_update_mode(graphs_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_div_line_count(graphs_chart, 6, 6);
  lv_obj_set_style_bg_color(graphs_chart, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_bg_grad_color(graphs_chart, lv_color_hex(0x111827), 0);
  lv_obj_set_style_bg_grad_dir(graphs_chart, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(graphs_chart, 1, 0);
  lv_obj_set_style_border_color(graphs_chart, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_pad_all(graphs_chart, 8, 0);
  lv_obj_set_style_pad_right(graphs_chart, 4, 0);

  lv_chart_set_axis_tick(
    graphs_chart,
    LV_CHART_AXIS_PRIMARY_Y,
    8,
    4,
    5,
    2,
    true,
    50
  );

  lv_chart_set_axis_tick(
    graphs_chart,
    LV_CHART_AXIS_PRIMARY_X,
    10,
    4,
    6,
    1,
    true,
    40
  );

  lv_obj_set_style_text_font(graphs_chart, LV_FONT_DEFAULT, LV_PART_TICKS);
  lv_obj_set_style_text_color(graphs_chart, lv_color_hex(0xCBD5F5), LV_PART_TICKS);

  lv_obj_set_style_line_color(graphs_chart, lv_color_hex(0x1F2937), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_opa(graphs_chart, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);

  graphs_series = nullptr;
  populate_graphs_chart(0);
  lv_dropdown_set_selected(graphs_parameter_dropdown, 0);

  lv_obj_t* summary_card = make_card(main_container);
  lv_obj_set_size(summary_card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_row(summary_card, 6, 0);
  lv_obj_t* summary_label = lv_label_create(summary_card);
  lv_label_set_text(summary_label,
                    "Trending tips:\n"
                    "• Use parameter selector to switch data sets\n"
                    "• Future release will support zoom & pan gestures\n"
                    "• Data export will be available via USB and Modbus logs");
  lv_obj_set_style_text_color(summary_label, lv_color_hex(0x9CA3AF), 0);
}

static void show_display_settings_popup() {
  // Check if display settings popup is already open
  for (int i = 0; i < lv_obj_get_child_cnt(content); i++) {
    lv_obj_t* child = lv_obj_get_child(content, i);
    if (child && lv_obj_is_valid(child)) {
      // Check if this is a display settings popup by looking for the title
      for (int j = 0; j < lv_obj_get_child_cnt(child); j++) {
        lv_obj_t* grandchild = lv_obj_get_child(child, j);
        if (grandchild && lv_obj_is_valid(grandchild)) {
          // Check if this is a label with "Display Settings" text
          if (lv_obj_get_class(grandchild) == &lv_label_class) {
            const char* text = lv_label_get_text(grandchild);
            if (text && strcmp(text, "Display Settings") == 0) {
              // Already open, just bring to front
              lv_obj_move_foreground(child);
              return;
            }
          }
        }
      }
    }
  }
  
  // Create a modal popup - 75% of screen size, centered in content area
  lv_obj_t* popup = lv_obj_create(content);  // Create relative to content area, not full screen
  lv_obj_set_size(popup, LV_PCT(75), LV_PCT(75));
  lv_obj_center(popup);  // Center the popup within the content area
  lv_obj_set_style_bg_color(popup, lv_color_hex(0x1B1B22), 0);
  lv_obj_set_style_border_color(popup, lv_color_hex(0x2A2A34), 0);
  lv_obj_set_style_border_width(popup, 2, 0);
  lv_obj_set_style_radius(popup, 16, 0);
  lv_obj_set_style_pad_all(popup, 30, 0);  // Increased padding for larger popup
  
  // Popup title - centered at top
  lv_obj_t* popup_title = lv_label_create(popup);
  lv_label_set_text(popup_title, "Display Settings");
  lv_obj_set_style_text_color(popup_title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(popup_title, LV_ALIGN_TOP_MID, 0, 20);  // Centered, with top margin
  
  // Edit Home Screen Layout button - centered in popup
  lv_obj_t* edit_home_btn = lv_btn_create(popup);
  lv_obj_set_size(edit_home_btn, 250, 60);  // Larger button for bigger popup
  lv_obj_align(edit_home_btn, LV_ALIGN_CENTER, 0, 0);  // Perfect center
  lv_obj_set_style_bg_color(edit_home_btn, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_radius(edit_home_btn, 12, 0);
  
  lv_obj_t* edit_home_label = lv_label_create(edit_home_btn);
  lv_label_set_text(edit_home_label, "Edit Home Screen Layout");
  lv_obj_center(edit_home_label);
  
  // Close button - centered at bottom
  lv_obj_t* close_btn = lv_btn_create(popup);
  lv_obj_set_size(close_btn, 100, 45);  // Larger close button
  lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -20);  // Centered at bottom with margin
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x6B7280), 0);
  lv_obj_set_style_radius(close_btn, 10, 0);
  
  lv_obj_t* close_label = lv_label_create(close_btn);
  lv_label_set_text(close_label, "Close");
  lv_obj_center(close_label);
  
  // Add event handlers
  lv_obj_add_event_cb(close_btn, on_close_popup, LV_EVENT_CLICKED, nullptr);
  
  lv_obj_add_event_cb(edit_home_btn, on_edit_home_layout, LV_EVENT_CLICKED, nullptr);
}

static void build_layout_preview(lv_obj_t* preview_container) {
  // Safety check
  if (!preview_container || !lv_obj_is_valid(preview_container)) {
    return;
  }
  
  // Create preview containers based on edit state
  for (size_t i = 0; i < INPUT_CHANNEL_COUNT; ++i) {
    InputChannel& channel = input_channels[i];
    if (!channel.editVisible) continue;

    lv_obj_t* container = lv_obj_create(preview_container);
    lv_obj_set_size(container, LV_PCT(100), 90);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(container, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_width(container, 1, 0);
    lv_obj_set_style_radius(container, 12, 0);
    lv_obj_set_style_pad_all(container, 15, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, 8, 0);

    char title_buf[48];
    if (channel.unit && channel.unit[0] != '\0') {
      snprintf(title_buf, sizeof(title_buf), "%s (%s)", channel.name, channel.unit);
    } else {
      snprintf(title_buf, sizeof(title_buf), "%s", channel.name);
    }

    lv_obj_t* title_lbl = lv_label_create(container);
    lv_label_set_text(title_lbl, title_buf);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x9CA3AF), 0);

    char value_buf[48];
    format_channel_value(channel, value_buf, sizeof(value_buf));

    lv_obj_t* value_lbl = lv_label_create(container);
    lv_label_set_text(value_lbl, value_buf);
    lv_obj_set_style_text_color(value_lbl, lv_color_hex(0xFFFFFF), 0);

    // Red minus button
    lv_obj_t* remove_btn = lv_btn_create(container);
    lv_obj_set_size(remove_btn, 34, 34);
    lv_obj_align(remove_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_set_style_bg_color(remove_btn, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_radius(remove_btn, 17, 0);

    lv_obj_t* minus_label = lv_label_create(remove_btn);
    lv_label_set_text(minus_label, "-");
    lv_obj_set_style_text_color(minus_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(minus_label);

    lv_obj_add_event_cb(remove_btn, on_remove_channel, LV_EVENT_CLICKED, &channel);
  }

  // Add container button (green plus) - always visible
  lv_obj_t* add_btn = lv_obj_create(preview_container);
  lv_obj_set_size(add_btn, 60, 60);
  lv_obj_set_style_bg_color(add_btn, lv_color_hex(0x10B981), 0);
  lv_obj_set_style_radius(add_btn, 30, 0);
  lv_obj_set_style_border_width(add_btn, 2, 0);
  lv_obj_set_style_border_color(add_btn, lv_color_hex(0x059669), 0);

  lv_obj_t* plus_label = lv_label_create(add_btn);
  lv_label_set_text(plus_label, "+");
  lv_obj_set_style_text_color(plus_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(plus_label);

  lv_obj_add_event_cb(add_btn, on_show_add_container_menu, LV_EVENT_CLICKED, nullptr);
}

static void show_home_layout_editor() {
  // Check if a layout editor is already open
  if (current_layout_popup && lv_obj_is_valid(current_layout_popup)) {
    // If one is already open, just bring it to front
    lv_obj_move_foreground(current_layout_popup);
    return;
  }
  
  // Initialize temporary state with current state
  for (size_t i = 0; i < INPUT_CHANNEL_COUNT; ++i) {
    input_channels[i].editVisible = input_channels[i].visible;
  }
  
  // Create a modal popup for home screen layout editing
  lv_obj_t* popup = lv_obj_create(content);
  
  // Store the popup reference globally so we can refresh it later
  current_layout_popup = popup;
  lv_obj_set_size(popup, LV_PCT(90), LV_PCT(90));
  lv_obj_center(popup);
  lv_obj_set_style_bg_color(popup, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_color(popup, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_border_width(popup, 3, 0);
  lv_obj_set_style_radius(popup, 20, 0);
  lv_obj_set_style_pad_all(popup, 25, 0);
  
  // Popup title
  lv_obj_t* popup_title = lv_label_create(popup);
  lv_label_set_text(popup_title, "Edit Home Screen Layout");
  lv_obj_set_style_text_color(popup_title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(popup_title, LV_ALIGN_TOP_MID, 0, 15);
  
  // Create a container for the layout preview
  lv_obj_t* preview_container = lv_obj_create(popup);
  lv_obj_set_size(preview_container, LV_PCT(100), LV_PCT(70));
  lv_obj_align(preview_container, LV_ALIGN_CENTER, 0, 20);
  lv_obj_set_style_bg_opa(preview_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(preview_container, 0, 0);
  lv_obj_set_flex_flow(preview_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(preview_container, 15, 0);
  
  // Mark this container so we can find it later for refreshing
  lv_obj_set_user_data(preview_container, (void*)0x1234);
  
  // Build the initial preview (includes the add button)
  build_layout_preview(preview_container);
  
  // Bottom buttons
  lv_obj_t* button_container = lv_obj_create(popup);
  lv_obj_set_size(button_container, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_align(button_container, LV_ALIGN_BOTTOM_MID, 0, -15);
  lv_obj_set_style_bg_opa(button_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(button_container, 0, 0);
  lv_obj_set_flex_flow(button_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(button_container, 15, 0);
  
  // Apply Changes button
  lv_obj_t* apply_btn = lv_btn_create(button_container);
  lv_obj_set_size(apply_btn, 120, 45);
  lv_obj_set_style_bg_color(apply_btn, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_radius(apply_btn, 10, 0);
  
  lv_obj_t* apply_label = lv_label_create(apply_btn);
  lv_label_set_text(apply_label, "Apply Changes");
  lv_obj_center(apply_label);
  
  // Close button
  lv_obj_t* close_btn = lv_btn_create(button_container);
  lv_obj_set_size(close_btn, 100, 45);
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x6B7280), 0);
  lv_obj_set_style_radius(close_btn, 10, 0);
  
  lv_obj_t* close_label = lv_label_create(close_btn);
  lv_label_set_text(close_label, "Close");
  lv_obj_center(close_label);
  
  // Button events
  lv_obj_add_event_cb(apply_btn, on_apply_changes, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(close_btn, on_close_popup, LV_EVENT_CLICKED, nullptr);
}

static void show_add_container_menu() {
  // Create a small popup to select which container to add back
  lv_obj_t* popup = lv_obj_create(content);
  lv_obj_set_size(popup, 250, 200);
  lv_obj_center(popup);
  lv_obj_set_style_bg_color(popup, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_border_color(popup, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_border_width(popup, 2, 0);
  lv_obj_set_style_radius(popup, 16, 0);
  lv_obj_set_style_pad_all(popup, 20, 0);
  lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(popup, 12, 0);
  lv_obj_set_style_pad_column(popup, 0, 0);
  
  // Title
  lv_obj_t* title = lv_label_create(popup);
  lv_label_set_text(title, "Add Container");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);
  
  bool any_hidden = false;
  for (size_t i = 0; i < INPUT_CHANNEL_COUNT; ++i) {
    InputChannel& channel = input_channels[i];
    if (channel.editVisible) continue;
    any_hidden = true;

    lv_obj_t* btn = lv_btn_create(popup);
    lv_obj_set_size(btn, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x374151), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_pad_all(btn, 8, 0);

    lv_obj_t* label = lv_label_create(btn);
    if (channel.unit && channel.unit[0] != '\0') {
      char name_buf[48];
      snprintf(name_buf, sizeof(name_buf), "%s (%s)", channel.name, channel.unit);
      lv_label_set_text(label, name_buf);
    } else {
      lv_label_set_text(label, channel.name);
    }
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, on_add_channel, LV_EVENT_CLICKED, &channel);
  }

  if (!any_hidden) {
    lv_obj_t* info = lv_label_create(popup);
    lv_label_set_text(info, "All containers are visible.");
    lv_obj_set_style_text_color(info, lv_color_hex(0x9CA3AF), 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -10);
  }
  
  // Close button
  lv_obj_t* close_btn = lv_btn_create(popup);
  lv_obj_set_size(close_btn, 80, 35);
  lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x6B7280), 0);
  lv_obj_set_style_radius(close_btn, 8, 0);
  
  lv_obj_t* close_label = lv_label_create(close_btn);
  lv_label_set_text(close_label, "Close");
  lv_obj_center(close_label);
  
  lv_obj_add_event_cb(close_btn, on_close_popup, LV_EVENT_CLICKED, nullptr);
}

static void rebuild_home_screen() {
  // Safety check - ensure page_home is valid
  if (!page_home || !lv_obj_is_valid(page_home)) {
    return;
  }

  // Clear the existing home page content
  lv_obj_clean(page_home);
  
  // Rebuild the home page with current visibility settings
  build_home_content(page_home);
  
  // The labels will be automatically updated by the timer on the next cycle
  // No need to manually set them to nullptr since they're recreated fresh
}

static void show_input_detail_popup(InputChannel* channel) {
  if (!channel) return;

  lv_obj_t* popup = lv_obj_create(content);
  lv_obj_set_size(popup, LV_PCT(75), LV_PCT(75));
  lv_obj_center(popup);
  lv_obj_set_style_bg_color(popup, lv_color_hex(0x1E1B4B), 0);
  lv_obj_set_style_border_color(popup, lv_color_hex(0x4338CA), 0);
  lv_obj_set_style_border_width(popup, 2, 0);
  lv_obj_set_style_radius(popup, 18, 0);
  lv_obj_set_style_pad_all(popup, 24, 0);
  lv_obj_set_style_pad_row(popup, 18, 0);
  lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);

  lv_obj_t* title = lv_label_create(popup);
  lv_label_set_text_fmt(title, "%s Details", channel->name);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

  char summary_buf[200];
  char value_buf[48];
  format_channel_value(*channel, value_buf, sizeof(value_buf));
  
  if (channel->calibrationOffset != 0.0f) {
    snprintf(summary_buf, sizeof(summary_buf),
             "Current reading: %s\nCalibration offset: %.1f %s\nOperating range: %.1f – %.1f %s\nAlarm window: %.1f – %.1f %s",
             value_buf,
             channel->calibrationOffset,
             channel->unit,
             channel->minValue,
             channel->maxValue,
             channel->unit,
             channel->warnLow,
             channel->warnHigh,
             channel->unit);
  } else {
    snprintf(summary_buf, sizeof(summary_buf),
             "Current reading: %s\nOperating range: %.1f – %.1f %s\nAlarm window: %.1f – %.1f %s",
             value_buf,
             channel->minValue,
             channel->maxValue,
             channel->unit,
             channel->warnLow,
             channel->warnHigh,
             channel->unit);
  }

  lv_obj_t* summary = lv_label_create(popup);
  lv_label_set_text(summary, summary_buf);
  lv_obj_set_style_text_color(summary, lv_color_hex(0xE5E7EB), 0);

  lv_obj_t* guidance = lv_label_create(popup);
  lv_label_set_text(guidance,
                    "Next actions (mock):\n"
                    "• Calibrate probe zero/span\n"
                    "• Adjust scaling against grab sample\n"
                    "• Review alarm thresholds\n"
                    "• Check last maintenance date");
  lv_obj_set_style_text_color(guidance, lv_color_hex(0xA5B4FC), 0);

  lv_obj_t* button_row = lv_obj_create(popup);
  lv_obj_set_size(button_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(button_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(button_row, 0, 0);
  lv_obj_set_style_pad_all(button_row, 0, 0);
  lv_obj_set_style_pad_column(button_row, 16, 0);
  lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);

  if (strcmp(channel->id, "sc") == 0) {
    lv_obj_t* zero_btn = lv_btn_create(button_row);
    lv_obj_set_size(zero_btn, 160, 44);
    lv_obj_set_style_bg_color(zero_btn, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_radius(zero_btn, 10, 0);
    lv_obj_add_event_cb(zero_btn, on_zero_calibration, LV_EVENT_CLICKED, channel);

    lv_obj_t* zero_lbl = lv_label_create(zero_btn);
    lv_label_set_text(zero_lbl, "Zero Calibration");
    lv_obj_center(zero_lbl);
  } else {
    lv_obj_t* calibrate_btn = lv_btn_create(button_row);
    lv_obj_set_size(calibrate_btn, 160, 44);
    lv_obj_set_style_bg_color(calibrate_btn, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_radius(calibrate_btn, 10, 0);

    lv_obj_t* calibrate_lbl = lv_label_create(calibrate_btn);
    lv_label_set_text(calibrate_lbl, "Start Calibration");
    lv_obj_center(calibrate_lbl);
  }

  lv_obj_t* silence_btn = lv_btn_create(button_row);
  lv_obj_set_size(silence_btn, 140, 44);
  lv_obj_set_style_bg_color(silence_btn, lv_color_hex(0xF97316), 0);
  lv_obj_set_style_radius(silence_btn, 10, 0);

  lv_obj_t* silence_lbl = lv_label_create(silence_btn);
  lv_label_set_text(silence_lbl, "Silence Alarm");
  lv_obj_center(silence_lbl);

  lv_obj_t* close_btn = lv_btn_create(popup);
  lv_obj_set_size(close_btn, 110, 42);
  lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x6B7280), 0);
  lv_obj_set_style_radius(close_btn, 10, 0);
  lv_obj_add_event_cb(close_btn, on_close_popup, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* close_lbl = lv_label_create(close_btn);
  lv_label_set_text(close_lbl, "Close");
  lv_obj_center(close_lbl);
}

static void build_config_content(lv_obj_t* page) {
  lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(page, 20, 0);
  lv_obj_set_style_pad_row(page, 20, 0);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* main_container = lv_obj_create(page);
  lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(main_container, 20, 0);
  lv_obj_set_style_pad_row(main_container, 20, 0);
  lv_obj_set_style_bg_opa(main_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(main_container, 0, 0);
  lv_obj_clear_flag(main_container, LV_OBJ_FLAG_SCROLLABLE);

  // Page title
  lv_obj_t* title = lv_label_create(main_container);
  lv_label_set_text(title, "System Configuration");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  // Use default font - no custom font setting needed
  
  // Display Settings button
  lv_obj_t* display_settings_btn = lv_btn_create(main_container);
  lv_obj_set_size(display_settings_btn, 250, 60);
  lv_obj_set_style_bg_color(display_settings_btn, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_radius(display_settings_btn, 12, 0);
  lv_obj_set_style_pad_all(display_settings_btn, 15, 0);
  
  lv_obj_t* display_settings_label = lv_label_create(display_settings_btn);
  lv_label_set_text(display_settings_label, "Display Settings");
  lv_obj_center(display_settings_label);
  
  // Add click event to open popup
  lv_obj_add_event_cb(display_settings_btn, on_show_display_settings, LV_EVENT_CLICKED, nullptr);
  
  // Display preferences card
  lv_obj_t* display_card = make_card(main_container);
  lv_obj_set_size(display_card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_row(display_card, 8, 0);
  lv_obj_t* display_info = lv_label_create(display_card);
  lv_label_set_text(display_info,
                    "Display preferences:\n"
                    "• Brightness scheduling (day/night)\n"
                    "• Theme presets for low light vs. outdoor\n"
                    "• Idle screen timeout and splash customization");
  lv_obj_set_style_text_color(display_info, lv_color_hex(0x9CA3AF), 0);

  // Alarm configuration card
  lv_obj_t* alarm_card = make_card(main_container);
  lv_obj_set_size(alarm_card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_row(alarm_card, 8, 0);
  lv_obj_t* alarm_info = lv_label_create(alarm_card);
  lv_label_set_text(alarm_info,
                    "Alarm configuration:\n"
                    "• Threshold editor per channel\n"
                    "• Relay behavior (latching, auto-reset)\n"
                    "• Audible alarm duration and silence timer");
  lv_obj_set_style_text_color(alarm_info, lv_color_hex(0x9CA3AF), 0);

  // Communications / maintenance card
  lv_obj_t* comm_card = make_card(main_container);
  lv_obj_set_size(comm_card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_row(comm_card, 8, 0);
  lv_obj_t* comm_info = lv_label_create(comm_card);
  lv_label_set_text(comm_info,
                    "Communications & maintenance:\n"
                    "• USB data export schedule\n"
                    "• Modbus / Ethernet setup wizard\n"
                    "• Auto-clean cycle interval and last run summary\n"
                    "• Firmware update & backup management");
  lv_obj_set_style_text_color(comm_info, lv_color_hex(0x9CA3AF), 0);
}

static void build_home() {
  page_home = lv_obj_create(content);
  build_home_content(page_home);
}

static void build_inputs() {
  page_inputs = lv_obj_create(content);
  build_inputs_content(page_inputs);
}

static void build_outputs() {
  page_outputs = lv_obj_create(content);
  build_outputs_content(page_outputs);
}

static void build_graphs() {
  page_graphs = lv_obj_create(content);
  build_graphs_content(page_graphs);
}

static void build_config() {
  page_config = lv_obj_create(content);
  build_config_content(page_config);
}

// ------------------- UI root -----------------------
static void build_ui() {
  root = lv_scr_act();
  lv_obj_set_style_bg_color(root, lv_color_hex(0x0A0A0F), 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);

  build_topbar();

  content = lv_obj_create(root);
  lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);

  build_home();
  build_inputs();
  build_outputs();
  build_graphs();
  build_config();
  
  // Start with home page visible
  show_page(page_home);
}

// ------------------- Hardware Input Functions ------------------
static float read_4_20ma_signal(uint8_t pin) {
  // Read analog value (0-4095 for 12-bit ADC)
  int raw_adc = analogRead(pin);
  
  // Convert to voltage
  float voltage = (raw_adc / ADC_RESOLUTION) * ADC_REFERENCE_VOLTAGE;
  
  // Convert voltage to current (4-20mA range)
  // Voltage = Current * Resistance
  float current_ma = (voltage / CURRENT_TO_VOLTAGE_RESISTOR) * 1000.0f; // Convert to mA
  
  // Clamp to valid 4-20mA range
  if (current_ma < 4.0f) current_ma = 4.0f;
  if (current_ma > 20.0f) current_ma = 20.0f;
  
  return current_ma;
}

static float scale_4_20ma_to_units(float current_ma, float min_units, float max_units) {
  // Linear scaling: 4mA = min_units, 20mA = max_units
  // Formula: units = min + (current - 4) / (20 - 4) * (max - min)
  float scaled = min_units + ((current_ma - 4.0f) / 16.0f) * (max_units - min_units);
  return scaled;
}

static float read_hardware_channel(uint8_t pin, float min_units, float max_units) {
  float current_ma = read_4_20ma_signal(pin);
  return scale_4_20ma_to_units(current_ma, min_units, max_units);
}

// ------------------- Timer update ------------------
static void ui_update_cb(lv_timer_t* t) {
#if USE_REAL_HARDWARE
  // Read from actual hardware inputs
  InputChannel& sc = input_channels[CHANNEL_STREAMING_CURRENT];
  // For particle counter or streaming current: scale 4-20mA to 0-1000 particles/mL or -500 to 500 mV
  // Adjust min/max based on your sensor's actual range
  sc.value = read_hardware_channel(PIN_4_20MA_CH1, sc.minValue, sc.maxValue);

  InputChannel& flow = input_channels[CHANNEL_FLOW];
  flow.value = read_hardware_channel(PIN_4_20MA_CH2, flow.minValue, flow.maxValue);

  InputChannel& ph = input_channels[CHANNEL_PH];
  ph.value = read_hardware_channel(PIN_4_20MA_CH3, ph.minValue, ph.maxValue);

  InputChannel& temp = input_channels[CHANNEL_TEMPERATURE];
  temp.value = read_hardware_channel(PIN_4_20MA_CH4, temp.minValue, temp.maxValue);
#else
  // Realistic fake dynamics for demo
  InputChannel& sc = input_channels[CHANNEL_STREAMING_CURRENT];
  // Streaming current typically centers around zero (optimal coagulant dosage)
  // Small fluctuations indicate charge neutralization is occurring
  sc.value = 0.0f + (random(-30, 31)) * 0.05f; // 0.0 ± 1.5 mV range, centered at zero

  InputChannel& flow = input_channels[CHANNEL_FLOW];
  flow.value += (random(-5, 6)) * 0.1f;
  if (flow.value < 100.0f) flow.value = 100.0f;
  if (flow.value > 140.0f) flow.value = 140.0f;

  InputChannel& ph = input_channels[CHANNEL_PH];
  ph.value += (random(-2, 3)) * 0.005f;
  if (ph.value < 7.0f) ph.value = 7.0f;
  if (ph.value > 7.4f) ph.value = 7.4f;

  InputChannel& temp = input_channels[CHANNEL_TEMPERATURE];
  temp.value += (random(-3, 4)) * 0.02f;
  if (temp.value < 20.0f) temp.value = 20.0f;
  if (temp.value > 25.0f) temp.value = 25.0f;
#endif

  // Update alarm state mock logic
  bool any_alarm = false;
  for (size_t i = 0; i < INPUT_CHANNEL_COUNT; ++i) {
    InputChannel& channel = input_channels[i];
    float calibrated_value = channel.value - channel.calibrationOffset;
    channel.alarmActive = (calibrated_value < channel.warnLow || calibrated_value > channel.warnHigh);
    any_alarm |= channel.alarmActive;

    push_trend_sample(static_cast<uint8_t>(i), channel.value);

    if (!alarm_state_initialized) {
      previous_alarm_state[i] = channel.alarmActive;
    } else {
      if (channel.alarmActive && !previous_alarm_state[i]) {
        record_alarm_event(channel, true);
      } else if (!channel.alarmActive && previous_alarm_state[i]) {
        record_alarm_event(channel, false);
      }
      previous_alarm_state[i] = channel.alarmActive;
    }
  }
  alarm_state_initialized = true;
  alarm_banner_visible = any_alarm;

  // Update labels only if they exist and are valid
  char buf[48];
  for (size_t i = 0; i < INPUT_CHANNEL_COUNT; ++i) {
    InputChannel& channel = input_channels[i];
    lv_obj_t* value_label = input_channel_ui[i].valueLabel;
    if (!value_label || !lv_obj_is_valid(value_label) || !channel.visible) continue;

    format_channel_value(channel, buf, sizeof(buf));
    lv_label_set_text(value_label, buf);
  }

  for (size_t i = 0; i < INPUT_CHANNEL_COUNT; ++i) {
    InputChannel& channel = input_channels[i];

    if (inputs_value_labels[i] && lv_obj_is_valid(inputs_value_labels[i])) {
      format_channel_value(channel, buf, sizeof(buf));
      lv_label_set_text(inputs_value_labels[i], buf);
    }

    if (inputs_status_labels[i] && lv_obj_is_valid(inputs_status_labels[i])) {
      lv_label_set_text(inputs_status_labels[i], channel.alarmActive ? "Alarm" : "Normal");
      lv_obj_set_style_text_color(
        inputs_status_labels[i],
        channel.alarmActive ? lv_color_hex(0xF87171) : lv_color_hex(0x34D399),
        0
      );
    }
  }

  update_home_banner();
  update_home_alarm_log();

  if (graphs_chart && lv_obj_is_valid(graphs_chart)) {
    uint16_t selected = 0;
    if (graphs_parameter_dropdown && lv_obj_is_valid(graphs_parameter_dropdown)) {
      selected = lv_dropdown_get_selected(graphs_parameter_dropdown);
      if (selected >= INPUT_CHANNEL_COUNT) {
        selected = 0;
      }
    }
    populate_graphs_chart(static_cast<uint8_t>(selected));
  }
}

// ------------------- Arduino setup/loop ------------
void setup() {
  Serial.begin(115200);
  
#if USE_REAL_HARDWARE
  // Initialize analog input pins for 4-20mA signals
  pinMode(PIN_4_20MA_CH1, INPUT);
  pinMode(PIN_4_20MA_CH2, INPUT);
  pinMode(PIN_4_20MA_CH3, INPUT);
  pinMode(PIN_4_20MA_CH4, INPUT);
  
  // Set analog reference (GIGA R1 uses 3.3V by default)
  analogReadResolution(12); // 12-bit resolution (0-4095)
  
  Serial.println("Hardware mode: Reading 4-20mA signals from analog inputs");
#else
  Serial.println("Demo mode: Using simulated sensor data");
#endif
  
  // Initialize display
  gfx.begin();
  gfx.setRotation(1);
  
  // Initialize touch
  Touch.begin();
  
  // Show splash screen first, then build UI after delay
  create_splash_screen();
  
  // Set up update timer
  lv_timer_create(ui_update_cb, 1000, nullptr);
}

void loop() {
  // Let LVGL do its thing
  lv_timer_handler();
  delay(5);
}
