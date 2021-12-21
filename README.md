# getplmux

This program attempts to fetch a list of Polish DVB-T transmitters from
[sat-charts.eu](https://sat-charts.eu/) and tries to tune to each of the
transmitters for each of the terrestial multiplexes, saving the resulting
transport stream into a file, and switching to the next multiplex after a
predefined time which is 30 seconds by default.

I created this as a replacement for a Bash script starting a pipeline via
`gst-launch` that I've always used when I wanted to capture DVB transmissions
from all the transmitters in my area.

# Usage

```
  -d, --duration                    Capture duration (in seconds)
  --location                        The location to lookup transmitters for as colon-separated latitude and longitude, for example : 52.393:16.857
  -r, --refresh                     Force refreshing cached transmitter data
  --dvbsrc-extra-params             Additional properties to apply to the dvbsrc element as a serialized GstStructure, for example : adapter=5,frontend=2
```

The fetched transmitter list is saved to the user's data directory when
successful, and the program will use that file by default instead of fetching
the list from scratch. If you've changed your location or want to re-fetch the
data, delete the file (`$XDG_DATA_HOME/getplmux/transmitters.xml`) or use `-r`.

# Disclaimer

This software is not endorsed by the author of
[sat-charts.eu](https://sat-charts.eu/).
