
idf_component_register(
    SRCS 
       
        "amaze_II_main"
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
    REQUIRES esp_partition esp_lcd esp_mm esp_timer
        )





