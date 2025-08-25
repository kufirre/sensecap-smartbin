/**
 * @file ui.h
 * @brief UI component for display management, button handling, and LED control
 * 
 * This component provides a unified interface for:
 * - LVGL display management with animated states
 * - Button initialization and callback management  
 * - RGB LED control for visual feedback and camera flash
 * 
 * The UI supports different visual states like listening, speaking, and status display,
 * along with hardware interaction through buttons and LEDs.
 */

#pragma once

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the UI system
 * 
 * Sets up the base LVGL objects and prepares the UI for use.
 * Creates label and image objects for state display.
 */
void ui_init(void);

/**
 * @brief Switch UI to speaking mode with animation
 * 
 * Displays animated speaking frames and hides status text.
 * Used during audio output/speaking phases.
 */
void ui_switch_speaking(void);

/**
 * @brief Switch UI to listening mode with animation
 * 
 * Displays animated listening frames with automatic cycling.
 * Used during audio input/listening phases.
 */
void ui_listening(void);

/**
 * @brief Display WiFi connecting status
 * 
 * Shows "Wi-Fi Connecting..." text and hides animations.
 * Used during network connection establishment.
 */
void ui_wifi_connecting(void);

/**
 * @brief Display WiFi connected status
 * 
 * Shows "WiFi Connected" text with success indication.
 * Used when WiFi connection is successfully established.
 */
void ui_wifi_connected(void);

/**
 * @brief Display WiFi configuration mode status
 * 
 * Shows "Configuration Mode" text to indicate device is in AP mode.
 * Used when device is acting as WiFi access point for setup.
 */
void ui_wifi_config_mode(void);

/**
 * @brief Display custom status text
 * 
 * Shows provided status text and hides animations.
 * Used for general status messages and feedback.
 * 
 * @param status_text Text to display on screen
 */
void ui_show_status(const char* status_text);

/**
 * @brief Initialize button with callback functions
 * 
 * Sets up the hardware button and registers callbacks for short and long press events.
 * Short press is typically used for quick actions like taking a picture.
 * Long press is typically used for power management or system functions.
 * 
 * @param short_press_cb Function to call on button short press/single click (can be NULL)
 * @param long_press_cb Function to call on button long press (can be NULL)
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ui_button_init(void (*short_press_cb)(void), void (*long_press_cb)(void));

/**
 * @brief Flash white LED for camera illumination
 * 
 * Turns on white LED at 5% brightness for specified duration, then turns it off.
 * Used as camera flash to improve image quality in low light conditions.
 * The reduced brightness prevents lens glare due to LED proximity to camera.
 * 
 * @param duration_ms Duration to keep LED on in milliseconds
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ui_camera_flash(uint32_t duration_ms);

/**
 * @brief Set RGB LED color
 * 
 * Sets the RGB LED to specified color values. Automatically initializes
 * the LED hardware if not already done.
 * 
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ui_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Turn off RGB LED
 * 
 * Convenience function to turn off the RGB LED by setting all colors to 0.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ui_rgb_off(void);

/**
 * @brief Check if device has physical display
 * 
 * Returns whether the device has a physical display for showing UI elements.
 * Used to conditionally enable/disable UI features based on hardware.
 * 
 * @return true if display is available, false otherwise
 */
bool ui_has_display(void);

#ifdef __cplusplus
}
#endif


