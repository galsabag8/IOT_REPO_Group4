## Conducting Baton Project by : Dor Abotbol, Shahar Payevsky & Gal Sabag
  
## Details about the project
In this project, we have developed an interactive system for learning musical conducting. The system allows the user to load a MIDI file and perform conducting gestures using a dedicated hardware baton. 
A custom-built GUI visualizes the conductor's movements in real-time to provide immediate feedback. Crucially, the system analyzes the user's gestures to dynamically adjust the music playback speed, ensuring the orchestra plays in sync with the conductor's rhythm.
 
## Folder description :
* Documentation: wiring diagram + basic operating instructions
* Unit Tests: tests for individual hardware components (input / output devices)
* Code : includes ESP32 source code for the esp side (firmware) and App code, the GUI written in HTML and backend in python (Flask)
* Parameters: contains description of parameters and settings that can be modified.
* Assets: MIDI files used in this project


## Hardware we used in the project:
* ESP32
* IMU 9 degrees of freedom - type BMX055
* Button

## Arduino/ESP32 libraries used in this project:
* SPI (Standard Arduino/ESP32 Core)
* Madgwick Algorithm - (Local implementation included in source)

Implementation Note:

Unlike standard implementations that rely on external sensor libraries (e.g., Adafruit), this project utilizes direct register access via SPI to communicate with the IMU sensors. This ensures minimal latency and optimized high-speed data acquisition required for real-time conducting.
Sensor fusion is handled via a local implementation of the Madgwick algorithm, adapted directly from the original source code rather than using the pre-compiled library.

## Connection diagram:
![](Documentation/connection%20diagram/Conducting_Baton_Wiring.png)

## Project Poster:
 
This project is part of ICST - The Interdisciplinary Center for Smart Technologies, Taub Faculty of Computer Science, Technion
https://icst.cs.technion.ac.il/
