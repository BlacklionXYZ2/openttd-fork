# Future Logistics NewGRFs that ship with the game. They are copied to the build directory's newgrf
# folder, which the game searches, and installed with the other data files. New games use Future
# Logistics and Starship by default when the configuration has no NewGRF list yet (see settings.cpp).
set(NEWGRF_SOURCE_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/future_logistics/future_logistics.grf
	${CMAKE_CURRENT_SOURCE_DIR}/starship/starship.grf
	${CMAKE_CURRENT_SOURCE_DIR}/corporate_infrastructure/CorporateInfrastructure.grf
)

foreach(NEWGRF_SOURCE_FILE IN LISTS NEWGRF_SOURCE_FILES)
    get_filename_component(NEWGRF_FILE_NAME "${NEWGRF_SOURCE_FILE}" NAME)
    set(NEWGRF_BINARY_FILE "${CMAKE_BINARY_DIR}/newgrf/${NEWGRF_FILE_NAME}")

    add_custom_command(OUTPUT ${NEWGRF_BINARY_FILE}
            COMMAND ${CMAKE_COMMAND} -E copy
                    ${NEWGRF_SOURCE_FILE}
                    ${NEWGRF_BINARY_FILE}
            MAIN_DEPENDENCY ${NEWGRF_SOURCE_FILE}
            COMMENT "Copying newgrf/${NEWGRF_FILE_NAME}"
    )

    list(APPEND NEWGRF_BINARY_FILES ${NEWGRF_BINARY_FILE})
endforeach()

# Create a new target which copies all shipped NewGRFs
add_custom_target(future_logistics_newgrfs
        DEPENDS ${NEWGRF_BINARY_FILES}
)

add_dependencies(openttd
    future_logistics_newgrfs
)
