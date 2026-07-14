#include "typx_executor.h"
#include "typx_protocol.h"
#include "typx_sha256.h"
#include "typx_state_machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

static int failures = 0;
static int assertions = 0;

#define CHECK(condition) \
  do { \
    ++assertions; \
    if (!(condition)) { \
      ++failures; \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
  } while (0)

typedef struct {
  uint8_t *bytes;
  size_t length;
} memory_file_t;

typedef struct {
  const char *name;
  const char *file_name;
  uint32_t action_count;
  uint32_t source_characters;
  uint64_t duration_us;
  const char *payload_sha256;
} valid_fixture_t;

typedef struct {
  const char *file_name;
  typx_protocol_error_t expected_error;
} invalid_fixture_t;

static const valid_fixture_t VALID_FIXTURES[] = {
    {"small-standard", "small-standard.bin", 11u, 8u, 2018000u,
     "a59a8aea945a245fd4a1768d5e6c389970259ba083151afc19a2350df493271c"},
    {"deterministic-personal", "deterministic-personal.bin", 14u, 11u,
     2422812u,
     "4cf8d2df52ae2648b50e740751284a2763b8aa001d45f5d4fe03d510a1f33127"},
    {"newline-and-final-cleanup", "newline-and-final-cleanup.bin", 10u, 3u,
     1160000u,
     "5205834cea6ac2467d8d1e70f6dad39b706cbd6e47b8ff059ef0d38e25958eab"}};

static const invalid_fixture_t INVALID_FIXTURES[] = {
    {"checksum-corruption.bin", TYPX_PROTOCOL_CHECKSUM_MISMATCH},
    {"truncation.bin", TYPX_PROTOCOL_PAYLOAD_SIZE_INVALID},
    {"incompatible-version.bin", TYPX_PROTOCOL_VERSION_INCOMPATIBLE},
    {"invalid-modifier.bin", TYPX_PROTOCOL_MODIFIER_INVALID},
    {"invalid-reserved-field.bin", TYPX_PROTOCOL_RESERVED_FIELD_NONZERO}};

static bool memory_read_at(
    void *opaque, uint64_t offset, uint8_t *destination, size_t length) {
  memory_file_t *file = opaque;
  if (file == NULL || destination == NULL || offset > file->length ||
      length > file->length - (size_t)offset) {
    return false;
  }
  memcpy(destination, file->bytes + (size_t)offset, length);
  return true;
}

static typx_reader_t memory_reader(memory_file_t *file) {
  typx_reader_t reader = {file, file->length, memory_read_at};
  return reader;
}

static memory_file_t load_fixture(const char *file_name) {
  char path[512];
  FILE *stream;
  long length;
  memory_file_t file = {NULL, 0u};
  snprintf(
      path,
      sizeof(path),
      "test_vectors/protocol-v1/%s",
      file_name);
  stream = fopen(path, "rb");
  if (stream == NULL) {
    fprintf(stderr, "Cannot open fixture %s\n", path);
    return file;
  }
  if (fseek(stream, 0, SEEK_END) != 0 || (length = ftell(stream)) < 0 ||
      fseek(stream, 0, SEEK_SET) != 0) {
    fclose(stream);
    return file;
  }
  file.bytes = malloc((size_t)length);
  if (file.bytes == NULL ||
      fread(file.bytes, 1u, (size_t)length, stream) != (size_t)length) {
    free(file.bytes);
    file.bytes = NULL;
    fclose(stream);
    return file;
  }
  file.length = (size_t)length;
  fclose(stream);
  return file;
}

static void free_file(memory_file_t *file) {
  free(file->bytes);
  file->bytes = NULL;
  file->length = 0u;
}

static void hex_digest(const uint8_t digest[32], char output[65]) {
  static const char HEX[] = "0123456789abcdef";
  size_t index;
  for (index = 0; index < 32u; ++index) {
    output[index * 2u] = HEX[digest[index] >> 4];
    output[index * 2u + 1u] = HEX[digest[index] & 0x0fu];
  }
  output[64] = '\0';
}

static typx_protocol_error_t verify_memory(
    memory_file_t *file,
    typx_protocol_limits_t limits,
    typx_verified_schedule_v1_t *verified) {
  typx_reader_t reader = memory_reader(file);
  typx_sha256_portable_context_t sha_context;
  typx_sha256_provider_t sha = typx_sha256_portable_provider(&sha_context);
  return typx_protocol_v1_verify(&reader, &limits, &sha, verified);
}

static void test_valid_fixtures(void) {
  size_t fixture_index;
  for (fixture_index = 0; fixture_index < ARRAY_COUNT(VALID_FIXTURES);
       ++fixture_index) {
    const valid_fixture_t *expected = &VALID_FIXTURES[fixture_index];
    memory_file_t file = load_fixture(expected->file_name);
    typx_verified_schedule_v1_t verified;
    char digest[65];
    uint32_t record_index;
    uint32_t previous_source = 0u;
    CHECK(file.bytes != NULL);
    CHECK(file.length >= TYPX_PROTOCOL_V1_HEADER_BYTES);
    CHECK(memcmp(file.bytes, "TYPXHID1", 8u) == 0);
    CHECK(file.bytes[8] == 1u);
    CHECK(file.bytes[10] == 1u);
    CHECK(file.bytes[12] == 96u && file.bytes[13] == 0u);
    CHECK(file.bytes[14] == 32u && file.bytes[15] == 0u);
    CHECK(
        verify_memory(
            &file, typx_protocol_v1_generic_limits(), &verified) ==
        TYPX_PROTOCOL_OK);
    CHECK(typx_protocol_v1_is_verified(&verified));
    CHECK(verified.header.action_count == expected->action_count);
    CHECK(verified.header.total_source_characters == expected->source_characters);
    CHECK(verified.header.total_expected_duration_us == expected->duration_us);
    hex_digest(verified.header.payload_sha256, digest);
    CHECK(strcmp(digest, expected->payload_sha256) == 0);

    for (record_index = 0; record_index < verified.header.action_count;
         ++record_index) {
      typx_protocol_record_v1_t record;
      CHECK(
          typx_protocol_v1_read_record(&verified, record_index, &record) ==
          TYPX_PROTOCOL_OK);
      CHECK(record.key_down_hold_us == 8000u);
      CHECK((record.flags & TYPX_RECORD_FLAG_RELEASE_ALL) != 0u);
      CHECK(record.source_index >= previous_source);
      CHECK(record.source_index <= expected->source_characters);
      previous_source = record.source_index;
    }
    free_file(&file);
  }
}

static void test_newline_action_order(void) {
  static const uint8_t modifiers[10] = {
      0u, 0u, 0u, 2u, 2u, 0u, 0u, 0u, 3u, 0u};
  static const uint8_t keycodes[10] = {
      0x04u, 0x29u, 0x28u, 0x4au, 0x4au,
      0x4cu, 0x05u, 0x29u, 0x4du, 0x4cu};
  memory_file_t file = load_fixture("newline-and-final-cleanup.bin");
  typx_verified_schedule_v1_t verified;
  uint32_t index;
  CHECK(
      verify_memory(&file, typx_protocol_v1_generic_limits(), &verified) ==
      TYPX_PROTOCOL_OK);
  for (index = 0; index < 10u; ++index) {
    typx_protocol_record_v1_t record;
    CHECK(
        typx_protocol_v1_read_record(&verified, index, &record) ==
        TYPX_PROTOCOL_OK);
    CHECK(record.modifier == modifiers[index]);
    CHECK(record.keycode == keycodes[index]);
    CHECK(
        ((record.flags & TYPX_RECORD_FLAG_FINAL_CLEANUP) != 0u) ==
        (index >= 7u));
  }
  free_file(&file);
}

static void test_invalid_fixtures(void) {
  size_t index;
  for (index = 0; index < ARRAY_COUNT(INVALID_FIXTURES); ++index) {
    memory_file_t file = load_fixture(INVALID_FIXTURES[index].file_name);
    typx_verified_schedule_v1_t verified;
    typx_protocol_error_t error = verify_memory(
        &file, typx_protocol_v1_generic_limits(), &verified);
    CHECK(error == INVALID_FIXTURES[index].expected_error);
    CHECK(!typx_protocol_v1_is_verified(&verified));
    free_file(&file);
  }
}

static void write_u16_le(uint8_t *bytes, uint16_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *bytes, uint64_t value) {
  write_u32_le(bytes, (uint32_t)value);
  write_u32_le(bytes + 4, (uint32_t)(value >> 32));
}

static void write_payload_digest(memory_file_t *file) {
  typx_sha256_portable_context_t context;
  typx_sha256_provider_t provider = typx_sha256_portable_provider(&context);
  uint8_t digest[32];
  CHECK(provider.begin(provider.context));
  CHECK(provider.update(
      provider.context,
      file->bytes + TYPX_PROTOCOL_V1_HEADER_BYTES,
      file->length - TYPX_PROTOCOL_V1_HEADER_BYTES));
  CHECK(provider.finish(provider.context, digest));
  memcpy(file->bytes + 56, digest, sizeof(digest));
}

static memory_file_t make_large_valid_schedule(uint32_t record_count) {
  memory_file_t file;
  uint32_t index;
  uint32_t payload_bytes = record_count * TYPX_PROTOCOL_V1_RECORD_BYTES;
  file.length = TYPX_PROTOCOL_V1_HEADER_BYTES + payload_bytes;
  file.bytes = calloc(file.length, 1u);
  if (file.bytes == NULL) {
    file.length = 0u;
    return file;
  }
  memcpy(file.bytes, "TYPXHID1", 8u);
  file.bytes[8] = 1u;
  file.bytes[9] = 0u;
  file.bytes[10] = 1u;
  file.bytes[11] = 1u;
  write_u16_le(file.bytes + 12, 96u);
  write_u16_le(file.bytes + 14, 32u);
  write_u32_le(file.bytes + 16, 3u);
  write_u32_le(file.bytes + 20, record_count);
  write_u32_le(file.bytes + 24, payload_bytes);
  write_u32_le(file.bytes + 28, 1u);
  write_u64_le(file.bytes + 32, (uint64_t)record_count * 8000u);
  for (index = 0; index < 16u; ++index) {
    file.bytes[40u + index] = (uint8_t)(index + 1u);
  }
  for (index = 0; index < record_count; ++index) {
    uint8_t *record = file.bytes + TYPX_PROTOCOL_V1_HEADER_BYTES +
        index * TYPX_PROTOCOL_V1_RECORD_BYTES;
    bool cleanup = index >= record_count - 3u;
    write_u32_le(record + 4, 8000u);
    write_u32_le(record + 12, 1u);
    record[16] = cleanup && index == record_count - 2u ? 3u : 0u;
    record[17] = cleanup
        ? (index == record_count - 3u
              ? 0x29u
              : (index == record_count - 2u ? 0x4du : 0x4cu))
        : 0x04u;
    write_u16_le(
        record + 18,
        cleanup
            ? TYPX_RECORD_FLAG_TECHNICAL |
                TYPX_RECORD_FLAG_FINAL_CLEANUP |
                TYPX_RECORD_FLAG_RELEASE_ALL
            : TYPX_RECORD_FLAG_SOURCE | TYPX_RECORD_FLAG_RELEASE_ALL);
    record[20] = 1u;
    record[21] = 1u;
  }
  write_payload_digest(&file);
  return file;
}

static void test_board_limits_and_excessive_delay(void) {
  memory_file_t large = make_large_valid_schedule(16382u);
  typx_verified_schedule_v1_t verified;
  CHECK(large.bytes != NULL);
  CHECK(large.length > TYPX_ESP32_CAM_MAX_SCHEDULE_BYTES);
  CHECK(
      verify_memory(&large, typx_protocol_v1_generic_limits(), &verified) ==
      TYPX_PROTOCOL_OK);
  CHECK(
      verify_memory(&large, typx_protocol_v1_esp32_cam_limits(), &verified) ==
      TYPX_PROTOCOL_SCHEDULE_LIMIT_EXCEEDED);
  free_file(&large);

  {
    memory_file_t delay = load_fixture("small-standard.bin");
    write_u32_le(
        delay.bytes + TYPX_PROTOCOL_V1_HEADER_BYTES,
        TYPX_PROTOCOL_V1_MAX_DELAY_US + 1u);
    write_payload_digest(&delay);
    CHECK(
        verify_memory(&delay, typx_protocol_v1_generic_limits(), &verified) ==
        TYPX_PROTOCOL_DELAY_INVALID);
    free_file(&delay);
  }
}

typedef struct {
  uint64_t now_us;
  unsigned wait_calls;
  unsigned fail_wait_call;
  unsigned key_down_calls;
  unsigned fail_key_down_call;
  unsigned release_calls;
  unsigned fail_release_call;
  unsigned progress_calls;
  unsigned stop_after_progress;
  unsigned terminal_calls;
  typx_terminal_status_t terminal_status;
  typx_executor_error_t terminal_error;
  typx_protocol_error_t terminal_protocol_error;
  char events[256];
  size_t event_count;
} fake_executor_t;

static void append_event(fake_executor_t *fake, char event) {
  if (fake->event_count < sizeof(fake->events)) {
    fake->events[fake->event_count++] = event;
  }
}

static uint64_t fake_now(void *opaque) {
  return ((fake_executor_t *)opaque)->now_us;
}

static bool fake_wait(void *opaque, uint32_t delay_us) {
  fake_executor_t *fake = opaque;
  ++fake->wait_calls;
  append_event(fake, 'W');
  if (fake->fail_wait_call == fake->wait_calls) {
    return false;
  }
  fake->now_us += delay_us;
  return true;
}

static bool fake_key_down(
    void *opaque, uint8_t modifier, uint8_t keycode) {
  fake_executor_t *fake = opaque;
  (void)modifier;
  (void)keycode;
  ++fake->key_down_calls;
  append_event(fake, 'K');
  return fake->fail_key_down_call != fake->key_down_calls;
}

static bool fake_release(void *opaque) {
  fake_executor_t *fake = opaque;
  ++fake->release_calls;
  append_event(fake, 'R');
  return fake->fail_release_call != fake->release_calls;
}

static bool fake_stop_requested(void *opaque) {
  fake_executor_t *fake = opaque;
  return fake->stop_after_progress > 0u &&
      fake->progress_calls >= fake->stop_after_progress;
}

static void fake_progress(
    void *opaque,
    uint32_t source_index,
    uint32_t completed_records,
    uint32_t total_records) {
  fake_executor_t *fake = opaque;
  (void)source_index;
  (void)completed_records;
  (void)total_records;
  ++fake->progress_calls;
  append_event(fake, 'P');
}

static void fake_terminal(
    void *opaque,
    typx_terminal_status_t status,
    typx_executor_error_t error,
    typx_protocol_error_t protocol_error) {
  fake_executor_t *fake = opaque;
  ++fake->terminal_calls;
  fake->terminal_status = status;
  fake->terminal_error = error;
  fake->terminal_protocol_error = protocol_error;
  append_event(fake, 'T');
}

static typx_executor_io_t fake_io(fake_executor_t *fake) {
  typx_executor_io_t io = {
      fake,
      fake_now,
      fake_wait,
      fake_key_down,
      fake_release,
      fake_stop_requested,
      fake_progress,
      fake_terminal};
  return io;
}

static bool event_order_has_release_before_progress(
    const fake_executor_t *fake) {
  size_t index;
  for (index = 0; index < fake->event_count; ++index) {
    if (fake->events[index] == 'P' &&
        (index == 0u || fake->events[index - 1u] != 'R')) {
      return false;
    }
  }
  return true;
}

static typx_verified_schedule_v1_t verified_fixture(
    memory_file_t *file, const char *name) {
  typx_verified_schedule_v1_t verified;
  *file = load_fixture(name);
  CHECK(
      verify_memory(file, typx_protocol_v1_esp32_cam_limits(), &verified) ==
      TYPX_PROTOCOL_OK);
  return verified;
}

static void test_executor_success_for_standard_and_personal(void) {
  static const char *fixtures[] = {
      "small-standard.bin", "deterministic-personal.bin"};
  size_t index;
  for (index = 0; index < ARRAY_COUNT(fixtures); ++index) {
    memory_file_t file;
    typx_verified_schedule_v1_t verified = verified_fixture(&file, fixtures[index]);
    fake_executor_t fake;
    typx_executor_io_t io;
    memset(&fake, 0, sizeof(fake));
    io = fake_io(&fake);
    CHECK(typx_executor_run(&verified, &io) == TYPX_EXECUTOR_OK);
    CHECK(fake.key_down_calls == verified.header.action_count);
    CHECK(fake.progress_calls == verified.header.action_count);
    CHECK(fake.release_calls == verified.header.action_count + 1u);
    CHECK(fake.terminal_calls == 1u);
    CHECK(fake.terminal_status == TYPX_TERMINAL_COMPLETED);
    CHECK(event_order_has_release_before_progress(&fake));
    free_file(&file);
  }
}

static void test_executor_failure_release_paths(void) {
  memory_file_t file;
  typx_verified_schedule_v1_t verified =
      verified_fixture(&file, "small-standard.bin");
  fake_executor_t fake;
  typx_executor_io_t io;

  memset(&fake, 0, sizeof(fake));
  fake.fail_key_down_call = 1u;
  io = fake_io(&fake);
  CHECK(typx_executor_run(&verified, &io) == TYPX_EXECUTOR_KEY_DOWN_FAILED);
  CHECK(fake.release_calls >= 1u);
  CHECK(fake.progress_calls == 0u);

  memset(&fake, 0, sizeof(fake));
  fake.fail_wait_call = 1u;
  io = fake_io(&fake);
  CHECK(typx_executor_run(&verified, &io) == TYPX_EXECUTOR_WAIT_FAILED);
  CHECK(fake.key_down_calls == 1u);
  CHECK(fake.release_calls >= 1u);
  CHECK(fake.progress_calls == 0u);

  memset(&fake, 0, sizeof(fake));
  fake.fail_release_call = 1u;
  io = fake_io(&fake);
  CHECK(typx_executor_run(&verified, &io) == TYPX_EXECUTOR_RELEASE_FAILED);
  CHECK(fake.release_calls >= 2u);
  CHECK(fake.progress_calls == 0u);

  memset(&fake, 0, sizeof(fake));
  fake.stop_after_progress = 1u;
  io = fake_io(&fake);
  CHECK(typx_executor_run(&verified, &io) == TYPX_EXECUTOR_STOPPED);
  CHECK(fake.progress_calls == 1u);
  CHECK(fake.release_calls >= 2u);
  CHECK(fake.terminal_status == TYPX_TERMINAL_ABORTED);

  memset(&fake, 0, sizeof(fake));
  io = fake_io(&fake);
  verified.verification_token = 0u;
  CHECK(typx_executor_run(&verified, &io) == TYPX_EXECUTOR_NOT_VERIFIED);
  CHECK(fake.release_calls == 1u);
  CHECK(fake.key_down_calls == 0u);

  free_file(&file);
}

static void test_executor_detects_mutated_verified_storage(void) {
  memory_file_t file;
  typx_verified_schedule_v1_t verified =
      verified_fixture(&file, "small-standard.bin");
  fake_executor_t fake;
  typx_executor_io_t io;
  file.bytes[TYPX_PROTOCOL_V1_HEADER_BYTES + 22u] = 1u;
  memset(&fake, 0, sizeof(fake));
  io = fake_io(&fake);
  CHECK(
      typx_executor_run(&verified, &io) ==
      TYPX_EXECUTOR_PROTOCOL_READ_FAILED);
  CHECK(fake.release_calls == 1u);
  CHECK(fake.key_down_calls == 0u);
  CHECK(
      fake.terminal_protocol_error ==
      TYPX_PROTOCOL_RESERVED_FIELD_NONZERO);
  free_file(&file);
}

typedef struct {
  unsigned releases;
  bool release_result;
} state_release_t;

static bool state_release(void *opaque) {
  state_release_t *release = opaque;
  ++release->releases;
  return release->release_result;
}

static void test_state_machine(void) {
  typx_state_machine_t machine;
  state_release_t release = {0u, true};
  typx_state_machine_boot(&machine);
  CHECK(machine.state == TYPX_STATE_BOOTING);
  CHECK(!machine.job_verified);
  CHECK(
      typx_state_machine_transition(&machine, TYPX_STATE_EXECUTING) ==
      TYPX_STATE_ILLEGAL_TRANSITION);
  CHECK(
      typx_state_machine_transition(&machine, TYPX_STATE_WIFI_READY) ==
      TYPX_STATE_OK);
  CHECK(
      typx_state_machine_transition(&machine, TYPX_STATE_RECEIVING) ==
      TYPX_STATE_OK);
  CHECK(
      typx_state_machine_transition(&machine, TYPX_STATE_VERIFYING) ==
      TYPX_STATE_OK);
  CHECK(
      typx_state_machine_transition(&machine, TYPX_STATE_READY) ==
      TYPX_STATE_JOB_NOT_VERIFIED);
  CHECK(
      typx_state_machine_mark_ready(&machine, true) ==
      TYPX_STATE_JOB_INCOMPLETE);
  typx_state_machine_set_upload_complete(&machine, true);
  CHECK(typx_state_machine_mark_ready(&machine, true) == TYPX_STATE_OK);
  CHECK(machine.state == TYPX_STATE_READY && machine.job_verified);
  CHECK(
      typx_state_machine_transition(&machine, TYPX_STATE_COUNTDOWN) ==
      TYPX_STATE_ILLEGAL_TRANSITION);
  CHECK(
      typx_state_machine_start(&machine) == TYPX_STATE_HID_NOT_CONNECTED);
  typx_state_machine_set_bluetooth_connected(&machine, true);
  CHECK(typx_state_machine_start(&machine) == TYPX_STATE_OK);
  CHECK(machine.state == TYPX_STATE_COUNTDOWN);
  CHECK(
      typx_state_machine_transition(&machine, TYPX_STATE_EXECUTING) ==
      TYPX_STATE_OK);
  CHECK(
      typx_state_machine_transition(&machine, TYPX_STATE_COMPLETED) ==
      TYPX_STATE_ILLEGAL_TRANSITION);
  CHECK(
      typx_state_machine_finish(
          &machine, TYPX_STATE_COMPLETED, state_release, &release) ==
      TYPX_STATE_OK);
  CHECK(release.releases == 1u);
  CHECK(machine.state == TYPX_STATE_COMPLETED);
  CHECK(!machine.job_verified);

  typx_state_machine_boot(&machine);
  CHECK(machine.state == TYPX_STATE_BOOTING);
  CHECK(!machine.upload_complete && !machine.job_verified);

  machine.state = TYPX_STATE_EXECUTING;
  release.release_result = false;
  CHECK(
      typx_state_machine_finish(
          &machine, TYPX_STATE_ABORTED, state_release, &release) ==
      TYPX_STATE_RELEASE_FAILED);
  CHECK(machine.state == TYPX_STATE_ERROR);
}

int main(void) {
  test_valid_fixtures();
  test_newline_action_order();
  test_invalid_fixtures();
  test_board_limits_and_excessive_delay();
  test_executor_success_for_standard_and_personal();
  test_executor_failure_release_paths();
  test_executor_detects_mutated_verified_storage();
  test_state_machine();

  if (failures == 0) {
    printf("PASS: %d assertions\n", assertions);
    return 0;
  }
  fprintf(stderr, "FAIL: %d of %d assertions failed\n", failures, assertions);
  return 1;
}
