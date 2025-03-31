if(TARGET ${PROJECT_NAME})
  target_sources(${PROJECT_NAME} PUBLIC FILE_SET public_headers TYPE HEADERS FILES modbus.h modbus-rtu.h modbus-tcp.h modbus-version.h)
  target_sources(${PROJECT_NAME} PRIVATE modbus.c modbus-data.c modbus-rtu.c modbus-tcp.c)

  install(TARGETS ${PROJECT_NAME} FILE_SET public_headers DESTINATION include/modbus)
endif()
