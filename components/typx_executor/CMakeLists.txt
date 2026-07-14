idf_component_register(
  SRCS "src/typx_executor.c" "src/typx_state_machine.c"
  INCLUDE_DIRS "include"
  REQUIRES typx_protocol
)
