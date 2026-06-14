# Optional full-flash erase before USB upload (fixes OTA boot-loop when otadata points at bad app1).
# Use: pio run -e node_min_c3_recover -t upload

Import("env")

if env.GetProjectOption("custom_erase_before_upload", "") == "yes":
    print("*** custom_erase_before_upload=yes: erasing entire flash ***")
    env.Execute("esptool.py erase_flash")
