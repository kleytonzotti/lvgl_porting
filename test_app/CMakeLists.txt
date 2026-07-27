cmake_minimum_required(VERSION 3.16)

# App de teste separado do firmware principal — roda os testes automatizados
# (Unity) dos módulos de lógica pura do projeto (app_ecu, app_pedal_link,
# app_sim). Não inclui UI/LVGL nem os componentes de hardware de tela.
#
# Rodar: idf.py -C test_app build flash monitor
set(EXTRA_COMPONENT_DIRS "../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_app)
