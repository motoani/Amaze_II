
idf_component_register(
    SRCS 
        "i80_lcd_main.cpp"
        "ShowWorld.cpp"
        "ClipBound.cpp"
        "CameraWork.cpp"
        "CheckTriangles.cpp"
        "ShowError.cpp"
        "TriangleQueues.cpp" 
        "wr_gpio.cpp" 
        "ChunkChooser.cpp"
        "RasteriseBox.cpp"
        "lcd_setup.c"
        "ParseWorld.cpp"
        "FindHitFace.cpp"  
        "TimeTracker.cpp"
        "EventManager.cpp"
       
    INCLUDE_DIRS
        "."
        "includes"
        "World"
    REQUIRES esp_partition esp_lcd esp_mm esp_timer
        )





