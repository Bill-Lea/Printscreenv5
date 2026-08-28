\# PrintScreen Plugin Configuration



The PrintScreen plugin supports customizable logging and behavior through an INI configuration file.



\## Configuration File Location



The configuration file is automatically created at:

```

C:\\Documents\\My Games\\Skyrim Special Edition\\SKSE\\PrintScreen.ini

```



\## Configuration Options



\### \[Logging] Section



\*\*LogLevel\*\* - Controls how much information is logged

\- `NONE` or `0` - No logging

\- `ERROR` or `1` - Error messages only  

\- `WARN` or `2` - Warning and error messages

\- `INFO` or `3` - Info, warning, and error messages (default)

\- `DEBUG` or `4` - All messages including debug output



Example:

```ini

\[Logging]

LogLevel=INFO

```



\### \[General] Section



\*\*EnablePerformanceLogging\*\* - Enable timing information for operations

\- `true` - Log how long capture operations take

\- `false` - Disable performance logging (default)



\*\*LogPapyrusCalls\*\* - Log all function calls from Papyrus scripts

\- `true` - Log all function calls (default)

\- `false` - Disable Papyrus call logging



Example:

```ini

\[General]

EnablePerformanceLogging=false

LogPapyrusCalls=true

```



\### \[Capture] Section



\*\*DefaultFormat\*\* - Default image format when not specified

\- Options: `PNG`, `JPG`, `BMP`, `TIF`, `GIF`, `DDS`

\- Default: `PNG`



\*\*DefaultQuality\*\* - Default JPEG quality when not specified

\- Range: 1-100

\- Default: 95



Example:

```ini

\[Capture]

DefaultFormat=PNG

DefaultQuality=95

```



\## Log File Location



Log output is written to:

```

C:\\Documents\\My Games\\Skyrim Special Edition\\SKSE\\printscreen.log

```



The log file is overwritten each time you start Skyrim to prevent excessive file growth.



\## Runtime Configuration Changes



You can reload the configuration without restarting Skyrim by calling the `ReloadConfig()` function from a Papyrus script (useful for mod developers).



\## Troubleshooting



1\. \*\*No log file created\*\*: Check that the SKSE folder exists and is writable

2\. \*\*Configuration not loading\*\*: Ensure the INI file syntax is correct (no spaces around `=`)

3\. \*\*Too much logging\*\*: Set `LogLevel=ERROR` or `LogLevel=NONE` to reduce output

4\. \*\*Performance issues\*\*: Disable debug logging with `LogLevel=INFO` or lower



\## Default Configuration



If no configuration file exists, the plugin will create one with these defaults:



```ini

; PrintScreen Plugin Configuration

\[Logging]

LogLevel=INFO



\[General]

EnablePerformanceLogging=false

LogPapyrusCalls=true



\[Capture]

DefaultFormat=PNG

DefaultQuality=95

```

