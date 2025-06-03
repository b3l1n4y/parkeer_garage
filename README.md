# Slimme Parkeergarage Systeem

Dit project bevat alle code voor een slim parkeersysteem met:
- **Arduino Uno R4 WiFi** (C++/PlatformIO): QR-scanner, sensoren, servo, OLED-display en voertuigdetectie.
- **Raspberry Pi 4** (Python/Flask): webapplicatie voor parkeerreserveringen, gebruikersbeheer en e-mailnotificaties.

## Mappenstructuur

```
/PlatformIO/ → Embedded code voor Arduino (C++/PlatformIO)
/PythonCode/ → Python webservercode voor Raspberry Pi (Flask)
```

## Gebruik

- Maak een reservering via de webapplicatie (Raspberry Pi)
- Scan de ontvangen QR-code bij de ingang
- Volg de instructies op het OLED-scherm
