foreach(required_variable IN ITEMS THEME_PATH BUTTON_PATH ICON_BUTTON_PATH SEGMENTED_PATH)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

file(READ "${THEME_PATH}" theme_qml)
file(READ "${BUTTON_PATH}" button_qml)
file(READ "${ICON_BUTTON_PATH}" icon_button_qml)
file(READ "${SEGMENTED_PATH}" segmented_qml)

foreach(expected_token IN ITEMS
        "textPrimary: \"#d8d8dc\""
        "textSecondary: \"#a3a3a8\""
        "textMuted: \"#818b94\""
        "textDisabled: \"#505158\""
        "textSelected: \"#49a7f2\"")
    string(FIND "${theme_qml}" "${expected_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "Missing ASTERDECK-style desktop theme token: ${expected_token}")
    endif()
endforeach()

foreach(expected_device_token IN ITEMS
        "deviceTitleText: textPrimary"
        "deviceBodyText: textPrimary"
        "deviceMutedText: textSecondary"
        "deviceDisabledText: textMuted"
        "deviceSelectedText: textSelected")
    string(FIND "${theme_qml}" "${expected_device_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "Missing device text hierarchy token: ${expected_device_token}")
    endif()
endforeach()

foreach(component_source IN ITEMS button_qml icon_button_qml segmented_qml)
    string(FIND "${${component_source}}" "AsterTheme.textDisabled" disabled_position)
    if(disabled_position EQUAL -1)
        message(FATAL_ERROR "${component_source} does not map its disabled state to textDisabled")
    endif()
endforeach()

foreach(component_source IN ITEMS icon_button_qml segmented_qml)
    string(FIND "${${component_source}}" "AsterTheme.textSelected" selected_position)
    if(selected_position EQUAL -1)
        message(FATAL_ERROR "${component_source} does not map its selected state to textSelected")
    endif()
endforeach()
