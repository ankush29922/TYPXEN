#include "typx_console.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "typx_ble_hid.h"
#include "typx_embedded_schedules.h"
#include "typx_hid_runtime.h"
#include "typx_wifi_service.h"

static const char *TAG = "typx_console";
static TaskHandle_t s_test_task;
static TaskHandle_t s_schedule_task;
static typx_console_command_t s_pending_test;
static typx_local_schedule_id_t s_pending_schedule;
static esp_console_repl_t *s_repl;

static void print_help(void) {
  puts("Commands:");
  puts("  help");
  puts("  status");
  puts("  release");
  puts("  stop");
  puts("  clear-bonds");
  puts("  test <8|12|16|20|30> <1..1080>");
  puts("  test-shift <8|12|16|20|30>  (types Aa!_{})");
  puts("  run-standard");
  puts("  run-personal");
  puts("  wifi-sta-config <ssid> <password>");
  puts("  wifi-sta-start");
  puts("  wifi-ap-start");
  puts("  wifi-stop");
  puts("  wifi-info");
  puts("  wifi-forget");
}

static void test_task(void *context) {
  typx_hid_runtime_t *runtime = typx_ble_hid_runtime();
  typx_hid_error_t result;
  (void)context;
  if (s_pending_test.type == TYPX_COMMAND_TEST) {
    result = typx_hid_run_test(
        runtime,
        s_pending_test.hold_ms,
        s_pending_test.character_count);
  } else {
    result = typx_hid_run_shift_test(runtime, s_pending_test.hold_ms);
  }
  typx_ble_hid_end_activity();
  ESP_LOGI(TAG, "Calibration result: %s", typx_hid_error_code(result));
  s_test_task = NULL;
  vTaskDelete(NULL);
}

static void start_test(const typx_console_command_t *command) {
  if (!typx_ble_hid_connected()) {
    ESP_LOGW(TAG, "Test refused: BLE HID host is not connected");
    return;
  }
  if (s_test_task != NULL || s_schedule_task != NULL ||
      !typx_hid_runtime_is_idle(typx_ble_hid_runtime())) {
    ESP_LOGW(TAG, "Test refused: another execution is running");
    return;
  }
  s_pending_test = *command;
  if (xTaskCreate(test_task, "typx_test", 4096, NULL, 5, &s_test_task) != pdPASS) {
    s_test_task = NULL;
    ESP_LOGE(TAG, "Unable to create calibration task");
  }
}

static void schedule_task(void *context) {
  typx_local_schedule_result_t result;
  const char *name = typx_embedded_schedule_name(s_pending_schedule);
  (void)context;
  result = typx_embedded_schedule_run(s_pending_schedule);
  if (result.protocol_error != TYPX_PROTOCOL_OK) {
    ESP_LOGE(
        TAG, "%s schedule rejected: %s",
        name, typx_protocol_error_code(result.protocol_error));
    (void)typx_hid_release_all(typx_ble_hid_runtime());
  } else {
    ESP_LOGI(
        TAG, "%s schedule result: %s",
        name, typx_executor_error_code(result.executor_error));
  }
  s_schedule_task = NULL;
  vTaskDelete(NULL);
}

static void start_schedule(typx_local_schedule_id_t schedule_id) {
  if (!typx_ble_hid_connected()) {
    ESP_LOGW(TAG, "Schedule refused: BLE HID host is not connected");
    return;
  }
  if (s_test_task != NULL || s_schedule_task != NULL ||
      !typx_hid_runtime_is_idle(typx_ble_hid_runtime())) {
    ESP_LOGW(TAG, "Schedule refused: another execution is running");
    return;
  }
  s_pending_schedule = schedule_id;
  if (xTaskCreate(
          schedule_task, "typx_schedule", 6144, NULL, 5,
          &s_schedule_task) != pdPASS) {
    s_schedule_task = NULL;
    ESP_LOGE(TAG, "Unable to create schedule task");
  }
}

static void execute_command(const typx_console_command_t *command) {
  typx_hid_runtime_t *runtime = typx_ble_hid_runtime();
  switch (command->type) {
    case TYPX_COMMAND_HELP:
      print_help();
      break;
    case TYPX_COMMAND_STATUS:
      ESP_LOGI(
          TAG, "BLE=%s execution=%s",
          typx_ble_hid_connected() ? "connected" : "disconnected",
          typx_hid_runtime_is_idle(runtime) ? "idle" : "running");
      break;
    case TYPX_COMMAND_RELEASE:
      ESP_LOGI(
          TAG, "Release result: %s",
          typx_hid_error_code(typx_hid_release_all(runtime)));
      break;
    case TYPX_COMMAND_STOP:
      typx_hid_runtime_request_stop(runtime);
      ESP_LOGI(
          TAG, "Stop requested; release result: %s",
          typx_hid_error_code(typx_hid_release_all(runtime)));
      break;
    case TYPX_COMMAND_CLEAR_BONDS:
      if (typx_wifi_service_job_locked()) {
        ESP_LOGW(TAG, "Bond clearing refused while a dedicated job is active");
        break;
      }
      if (!typx_hid_runtime_can_clear_bonds(runtime)) {
        ESP_LOGW(TAG, "Bond clearing requires an idle, disconnected device");
      } else {
        ESP_LOGI(
            TAG, "Clear bonds result: %s",
            typx_ble_hid_clear_bonds() ? "OK" : "FAILED_OR_CONNECTED");
      }
      break;
    case TYPX_COMMAND_TEST:
    case TYPX_COMMAND_TEST_SHIFT:
      start_test(command);
      break;
    case TYPX_COMMAND_RUN_STANDARD:
      start_schedule(TYPX_LOCAL_STANDARD);
      break;
    case TYPX_COMMAND_RUN_PERSONAL:
      start_schedule(TYPX_LOCAL_PERSONAL);
      break;
    default:
      ESP_LOGW(TAG, "Invalid command; enter help");
      break;
  }
}

static int command_handler(int argc, char **argv) {
  typx_console_command_t command;
  if (!typx_console_parse_argv(
          argc, (const char *const *)argv, &command)) {
    ESP_LOGW(TAG, "Malformed or unsupported command; enter help");
    return 1;
  }
  execute_command(&command);
  return 0;
}

static int wifi_command_handler(int argc, char **argv) {
  esp_err_t error;
  if (strcmp(argv[0], "wifi-sta-config") == 0) {
    if (argc != 3) {
      ESP_LOGW(TAG, "Usage: wifi-sta-config <ssid> <password>");
      return 1;
    }
    error = typx_wifi_service_configure_station(argv[1], argv[2]);
  } else if (argc != 1) {
    ESP_LOGW(TAG, "%s does not accept arguments", argv[0]);
    return 1;
  } else if (strcmp(argv[0], "wifi-sta-start") == 0) {
    error = typx_wifi_service_start_station();
  } else if (strcmp(argv[0], "wifi-ap-start") == 0) {
    error = typx_wifi_service_start_ap();
  } else if (strcmp(argv[0], "wifi-stop") == 0) {
    error = typx_wifi_service_stop();
  } else if (strcmp(argv[0], "wifi-forget") == 0) {
    error = typx_wifi_service_forget_station();
  } else if (strcmp(argv[0], "wifi-info") == 0) {
    typx_wifi_service_print_info();
    return 0;
  } else {
    ESP_LOGW(TAG, "Unsupported Wi-Fi command");
    return 1;
  }
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "%s failed: %s", argv[0], esp_err_to_name(error));
    return 1;
  }
  ESP_LOGI(TAG, "%s completed", argv[0]);
  return 0;
}

static esp_err_t register_commands(void) {
  static const esp_console_cmd_t commands[] = {
    {
      .command = "help",
      .help = "List Typx calibration commands",
      .func = command_handler
    },
    {
      .command = "status",
      .help = "Show BLE and execution state",
      .func = command_handler
    },
    {
      .command = "release",
      .help = "Send an empty HID keyboard report",
      .func = command_handler
    },
    {
      .command = "stop",
      .help = "Stop execution and release all keys",
      .func = command_handler
    },
    {
      .command = "clear-bonds",
      .help = "Clear BLE bonds while idle and disconnected",
      .func = command_handler
    },
    {
      .command = "test",
      .help = "Run calibration: test <hold_ms> <character_count>",
      .hint = "<hold_ms> <character_count>",
      .func = command_handler
    },
    {
      .command = "test-shift",
      .help = "Run modifier calibration: test-shift <hold_ms>",
      .hint = "<hold_ms>",
      .func = command_handler
    },
    {
      .command = "run-standard",
      .help = "Verify and run the embedded Standard golden schedule",
      .func = command_handler
    },
    {
      .command = "run-personal",
      .help = "Verify and run the embedded Personal golden schedule",
      .func = command_handler
    },
    {
      .command = "wifi-sta-config",
      .help = "Save phone hotspot SSID and WPA2 password",
      .func = wifi_command_handler
    },
    {
      .command = "wifi-sta-start",
      .help = "Connect to the saved phone hotspot",
      .func = wifi_command_handler
    },
    {
      .command = "wifi-ap-start",
      .help = "Start the recovery upload access point",
      .func = wifi_command_handler
    },
    {
      .command = "wifi-stop",
      .help = "Stop the upload HTTP service and ESP32-CAM Wi-Fi radio",
      .func = wifi_command_handler
    },
    {
      .command = "wifi-info",
      .help = "Show Wi-Fi mode, saved network, recovery AP, and IP",
      .func = wifi_command_handler
    },
    {
      .command = "wifi-forget",
      .help = "Forget the saved phone hotspot and use recovery AP",
      .func = wifi_command_handler
    }
  };
  size_t index;
  esp_err_t error;
  for (index = 0; index < sizeof(commands) / sizeof(commands[0]); ++index) {
    error = esp_console_cmd_register(&commands[index]);
    if (error != ESP_OK) {
      return error;
    }
  }
  return ESP_OK;
}

esp_err_t typx_console_start(void) {
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  esp_console_dev_uart_config_t uart_config =
      ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  esp_err_t error;
  repl_config.prompt = "typx> ";
  repl_config.max_cmdline_length = 128u;
  error = register_commands();
  if (error != ESP_OK) {
    return error;
  }
  error = esp_console_new_repl_uart(&uart_config, &repl_config, &s_repl);
  if (error != ESP_OK) {
    return error;
  }
  print_help();
  return esp_console_start_repl(s_repl);
}
