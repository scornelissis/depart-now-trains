# DEPARTNOW

A small M5Cardputer program which allows you to check when to leave to catch a train based on your location, your transport method and the train departure time.

## Info you need to get it running

### API KEY
 
Get an api key for the routes from here: https://openrouteservice.org/
and paste the key at this line of code: `const char* ROUTE_API_KEY = "YOURAPIKEYHERE";`

### LOCATION INFO

Please put your the latitude and longitude of your location here:

```c
const float OWN_LAT = your_latitude;
const float OWN_LON = your_longitude;
```

### WIFI Connection for M5Cardputer

Please put your WiFi details here:
```c
const char* ssid     = "wifi_ssid";
const char* password = "wifi_password";
```


## TO DO

- Run a https server instead of an http server so that the browser can ask location of the user through geolocation api to get a dynamic location
- Save destination and time in EEPROM since a lot of trains ride on the same hours every day no reconfiguration would need to happen through the browser
