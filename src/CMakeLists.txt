nap_desktop_app(calantas "" NRC_NONE)
target_include_directories(calantas PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(CALANTAS_XMPP_TARGET)
  target_compile_definitions(calantas PRIVATE CALANTAS_HAVE_XMPP=1)
  target_link_libraries(calantas PRIVATE ${CALANTAS_XMPP_TARGET})
endif()
