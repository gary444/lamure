############################################################
# CMake Build Script for the lod_converter executable

link_directories(${SCHISM_LIBRARY_DIRS})

include_directories(${REND_INCLUDE_DIR} 
                    ${COMMON_INCLUDE_DIR}
                    ${GLEW_INCLUDE_DIRS})

include_directories(SYSTEM ${SCHISM_INCLUDE_DIRS}
						   ${Boost_INCLUDE_DIR})


InitApp(${CMAKE_PROJECT_NAME}_rectangle_packing)

############################################################
# Libraries

target_link_libraries(${PROJECT_NAME}
    ${PROJECT_LIBS}
    ${REND_LIBRARY}
    "GL"
    "GLEW"
    ${OPENGL_LIBRARIES} 
    ${GLUT_LIBRARY}
    ${OpenGL_LIBRARIES} 
    ${GLUT_LIBRARY}
    ${GLEW_LIBRARY}
    ${GLEW_LIBRARIES}
    )

