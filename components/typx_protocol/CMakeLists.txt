idf_component_register(
  SRCS "src/typx_protocol.c" "src/typx_sha256_mbedtls.c"
  INCLUDE_DIRS "include"
  REQUIRES mbedtls
)
