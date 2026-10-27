# Bluetooth Notes

## Privacy-Enhanced Addressing

Modern Bluetooth devices use resolvable private addresses (RPA) for privacy.
The MAC address rotates periodically (typically every 15 minutes). btlock
handles this by:

1. Tracking devices by their *current* address in BlueZ.
2. Using the BlueZ `Alias` property (which is stable per paired device) as
   a secondary correlation key.
3. Pairing devices in BlueZ before enrolling them — paired devices have
   stable identifiers even as their MAC rotates.

**Enrollment workflow**:

```bash
# 1. Pair the device
bluetoothctl pair AA:BB:CC:DD:EE:FF

# 2. Note the address that appears
bluetoothctl devices

# 3. Add it to config
btlockd --setup  # then edit ~/.config/btlock/config.json
```

## BlueZ D-Bus API

btlock uses the BlueZ D-Bus API exposed on the system bus:

```
Service:     org.bluez
Object path: /org/bluez/hci0
Interface:   org.bluez.Adapter1
Methods:     StartDiscovery() / StopDiscovery()

Object path: /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF
Interface:   org.bluez.Device1
Properties:  Address, Name, RSSI, Paired, Connected, Alias
```

The monitor subscribes to `PropertiesChanged` signals on the adapter and
all device objects.

## Signal Strength Characteristics

Bluetooth RSSI is noisy. Typical characteristics:

- **Variance**: ±3 to ±10 dBm on a stationary device.
- **Body blocking**: Can cause 20–40 dBm drops when the user's body
  blocks the signal.
- **Multipath**: Signals bounce off surfaces, causing constructive/destructive
  interference that can swing RSSI by 15–30 dBm over short distances.
- **Wall attenuation**: Different materials attenuate differently:
  - Wood door: -3 to -10 dBm
  - Concrete wall: -15 to -30 dBm
  - Metal barrier: -40+ dBm

## Recommended Thresholds

For typical office/home environments:

```json
{
  "absent_threshold_dbm": -90,
  "present_threshold_dbm": -80,
  "return_threshold_dbm": -75,
  "rssi_window": 5,
  "filter_type": "median",
  "min_disconnects": 3
}
```

For high-noise environments (e.g., device on a keychain):

```json
{
  "absent_threshold_dbm": -85,
  "present_threshold_dbm": -75,
  "return_threshold_dbm": -70,
  "rssi_window": 8,
  "min_disconnects": 5
}
```

## Troubleshooting

### Device not detected

1. Check BlueZ is running: `systemctl status bluetooth`
2. Check device is paired: `bluetoothctl paired-devices`
3. Check signal: bring device closer and run `bluetoothctl` → `info AA:BB:CC:DD:EE:FF`
4. Ensure device is in the `enrolled_devices` list.

### False locks

1. Increase `lock_grace_sec` (default 15s may be too short for some users).
2. Increase `min_disconnects` to require more consecutive absent readings.
3. Switch `filter_type` to `moving_average` for smoother readings.
4. Increase `rssi_window` for more averaging.

### False unlocks / no lock

1. Increase `absent_threshold_dbm` (e.g., -85) to lock sooner.
2. Decrease `present_threshold_dbm` (e.g., -85) to require stronger signal.
3. Decrease `lock_grace_sec` for faster locking.

### Auto-unlock not working

Auto-unlock requires:
- `auto_unlock_enabled: true` in config.
- Desktop session to support logind `Unlock` D-Bus call.
- User to authenticate at the lock screen (password/biometrics).
