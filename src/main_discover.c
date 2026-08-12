// weather-life/src/main_discover.c
// Main executable to discover and test weather device

#include "usb_device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #define SLEEP(ms) Sleep(ms)
#else
    #include <unistd.h>
    #define SLEEP(ms) usleep((ms) * 1000)
#endif

int main(int argc, char* argv[])
{
    printf("="
);
    printf("Weather-Life USB Device Discoverer\n");
    printf("="
);
    printf("\n");
    
    // Get USB backend for current platform
    USBBackend* backend = usb_get_backend();
    if (!backend) {
        fprintf(stderr, "Error: No USB backend available for this platform\n");
        return 1;
    }
    
    printf("[*] Platform detected: ");
    #ifdef WEATHER_LIFE_WINDOWS
        printf("Windows (WinUSB)\n");
    #elif WEATHER_LIFE_LINUX
        printf("Linux (libusb)\n");
    #elif WEATHER_LIFE_MACOS
        printf("macOS (IOKit)\n");
    #else
        printf("Unknown\n");
    #endif
    
    printf("[*] Enumerating known weather devices...\n\n");
    
    // Enumerate devices
    WeatherDeviceID* devices = NULL;
    int device_count = 0;
    
    if (!backend->enumerate_devices(&devices, &device_count)) {
        fprintf(stderr, "Error: Failed to enumerate devices\n");
        return 1;
    }
    
    printf("Known device IDs:\n");
    for (int i = 0; i < device_count && devices[i].vid != 0; i++) {
        printf("  %d. VID:0x%04x PID:0x%04x - %s\n",
               i + 1, devices[i].vid, devices[i].pid, devices[i].name);
        printf("     Description: %s\n", devices[i].description);
    }
    printf("\n");
    
    // Try to find and open device
    printf("[*] Attempting to locate weather device...\n");
    
    USBDevice device;
    memset(&device, 0, sizeof(USBDevice));
    
    if (!usb_find_weather_device(&device)) {
        fprintf(stderr, "\n[!] Error: Weather device not found!\n");
        fprintf(stderr, "Troubleshooting:\n");
        fprintf(stderr, "  1. Plug in the weather dongle\n");
        fprintf(stderr, "  2. Wait 2-3 seconds for device to enumerate\n");
        fprintf(stderr, "  3. Run again\n");
        
        printf("\nWindows: Check Device Manager for unknown devices\n");
        printf("Linux: Run 'lsusb' to see connected USB devices\n");
        printf("macOS: Run 'system_profiler SPUSBDataType'\n");
        
        backend->free_device_list(devices);
        return 1;
    }
    
    printf("\n[+] SUCCESS! Found device:\n");
    printf("    VID: 0x%04x\n", device.vid);
    printf("    PID: 0x%04x\n", device.pid);
    printf("    Path: %s\n", device.device_path);
    printf("    Timeout: %d ms\n", device.timeout_ms);
    
    // Try to initialize device
    printf("\n[*] Initializing device...\n");
    
    if (!usb_init_device(&device)) {
        fprintf(stderr, "[!] Warning: Device initialization may have failed\n");
        fprintf(stderr, "    But device is still open - trying to continue\n");
    }
    
    // Test sending command
    printf("\n[*] Testing USB communication...\n");
    
    uint8_t test_data[] = {0x01, 0x02, 0x03};
    if (backend->send_command(&device, CAL_USB_READ, test_data, sizeof(test_data))) {
        printf("[+] Successfully sent test command\n");
    } else {
        printf("[-] Test command failed (device may not support this)\n");
    }
    
    // Try to get status
    uint8_t status = 0;
    SLEEP(500);  // Wait for response
    
    if (backend->get_status(&device, &status)) {
        printf("[+] Device status: 0x%02x\n", status);
    } else {
        printf("[-] Could not get device status\n");
    }
    
    // Send sample weather data
    printf("\n[*] Testing weather data transmission...\n");
    
    WeatherData sample_weather = {
        .temperature = 22.5f,
        .humidity = 65,
        .wind_speed = 3.2f,
        .wind_direction = 180,
        .weather_code = 0x02,  // Cloudy
        .pressure = 1013
    };
    
    if (usb_send_weather_data(&device, &sample_weather)) {
        printf("[+] Successfully sent weather data:\n");
        printf("    Temperature: %.1f C\n", sample_weather.temperature);
        printf("    Humidity: %d%%\n", sample_weather.humidity);
        printf("    Wind: %.1f m/s at %d degrees\n",
               sample_weather.wind_speed, sample_weather.wind_direction);
    } else {
        printf("[-] Failed to send weather data\n");
    }
    
    // Close device
    printf("\n[*] Closing device...\n");
    usb_close(&device);
    
    backend->free_device_list(devices);
    
    printf("\n" "="
);
    printf("Device test completed successfully!\n");
    printf("="
);
    printf("\n");
    
    return 0;
}
