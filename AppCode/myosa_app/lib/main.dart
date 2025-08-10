import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:syncfusion_flutter_charts/charts.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:vibration/vibration.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:permission_handler/permission_handler.dart';

void main() {
  runApp(const MyosaApp());
}

class MyosaApp extends StatelessWidget {
  const MyosaApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Myosa Gait Monitor',
      theme: ThemeData(
        primarySwatch: Colors.blue,
        visualDensity: VisualDensity.adaptivePlatformDensity,
      ),
      home: const PermissionWrapper(),
    );
  }
}

class PermissionWrapper extends StatefulWidget {
  const PermissionWrapper({super.key});

  @override
  State<PermissionWrapper> createState() => _PermissionWrapperState();
}

class _PermissionWrapperState extends State<PermissionWrapper> {
  bool _permissionsGranted = false;

  @override
  void initState() {
    super.initState();
    _checkPermissions();
  }

  Future<void> _checkPermissions() async {
    Map<Permission, PermissionStatus> statuses = await [
      Permission.bluetooth,
      Permission.bluetoothConnect,
      Permission.bluetoothScan,
      Permission.locationWhenInUse,
    ].request();

    if (statuses.values.every((status) => status.isGranted)) {
      setState(() {
        _permissionsGranted = true;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return _permissionsGranted 
        ? const DeviceScanScreen() 
        : Scaffold(
            body: Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Text('Permissions required'),
                  ElevatedButton(
                    onPressed: _checkPermissions,
                    child: const Text('Retry'),
                  ),
                ],
              ),
            ),
          );
  }
}

class DeviceScanScreen extends StatefulWidget {
  const DeviceScanScreen({super.key});

  @override
  State<DeviceScanScreen> createState() => _DeviceScanScreenState();
}

class _DeviceScanScreenState extends State<DeviceScanScreen> {
  List<ScanResult> _scanResults = [];
  bool _isScanning = false;
  String _status = "Tap to scan";

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Myosa Device Scanner'),
        actions: [
          IconButton(
            icon: const Icon(Icons.settings),
            onPressed: () => Navigator.push(
              context,
              MaterialPageRoute(builder: (context) => const SettingsScreen()),
            ),
        ],
      ),
      body: Column(
        children: [
          Padding(
            padding: const EdgeInsets.all(16.0),
            child: Text(_status, style: const TextStyle(fontSize: 18)),
          ),
          Expanded(
            child: ListView.builder(
              itemCount: _scanResults.length,
              itemBuilder: (context, index) {
                final device = _scanResults[index].device;
                return ListTile(
                  title: Text(device.name.isEmpty ? 'Unknown Device' : device.name),
                  subtitle: Text(device.remoteId.str),
                  leading: const Icon(Icons.watch),
                  onTap: () => _connectToDevice(device),
                );
              },
            ),
          ),
          Padding(
            padding: const EdgeInsets.all(16.0),
            child: ElevatedButton(
              onPressed: _isScanning ? null : _startScan,
              child: Text(_isScanning ? 'Scanning...' : 'Scan for Devices'),
            ),
          ),
        ],
      ),
    );
  }

  Future<void> _startScan() async {
    setState(() {
      _isScanning = true;
      _scanResults.clear();
      _status = "Scanning...";
    });

    FlutterBluePlus.scanResults.listen((results) {
      setState(() {
        _scanResults = results.where((r) => r.device.name.isNotEmpty).toList();
      });
    });

    await FlutterBluePlus.startScan(
      timeout: const Duration(seconds: 15),
      androidUsesFineLocation: false,
    );

    setState(() {
      _isScanning = false;
      _status = "Found ${_scanResults.length} device(s)";
    });
  }

  Future<void> _connectToDevice(BluetoothDevice device) async {
    try {
      await device.connect(autoConnect: false);
      Navigator.push(
        context,
        MaterialPageRoute(
          builder: (context) => DeviceDashboard(device: device),
        ),
      );
    } catch (e) {
      setState(() {
        _status = "Connection failed: ${e.toString()}";
      });
    }
  }
}

class DeviceDashboard extends StatefulWidget {
  final BluetoothDevice device;

  const DeviceDashboard({super.key, required this.device});

  @override
  State<DeviceDashboard> createState() => _DeviceDashboardState();
}

class _DeviceDashboardState extends State<DeviceDashboard> {
  BluetoothCharacteristic? _dataCharacteristic;
  bool _isConnected = false;
  String _connectionStatus = "Connecting...";
  
  int _steps = 0;
  double _cadence = 0;
  double _lastAccel = 0;
  String _status = "Normal";
  List<double> _accelHistory = List.filled(50, 0);
  List<Map<String, dynamic>> _events = [];
  
  String _emergencyContact = "";
  String _medicalNotes = "";

  @override
  void initState() {
    super.initState();
    _loadSettings();
    _connectToDevice();
  }

  Future<void> _loadSettings() async {
    final prefs = await SharedPreferences.getInstance();
    setState(() {
      _emergencyContact = prefs.getString('emergencyContact') ?? "";
      _medicalNotes = prefs.getString('medicalNotes') ?? "";
    });
  }

  Future<void> _connectToDevice() async {
    try {
      await widget.device.connect();
      setState(() {
        _isConnected = true;
        _connectionStatus = "Discovering services...";
      });

      List<BluetoothService> services = await widget.device.discoverServices();
      BluetoothService? service = services.firstWhere(
        (s) => s.uuid == Guid("0000FFE0-0000-1000-8000-00805F9B34FB"),
        orElse: () => throw Exception("Service not found"),
      );

      _dataCharacteristic = service.characteristics.firstWhere(
        (c) => c.uuid == Guid("0000FFE1-0000-1000-8000-00805F9B34FB"),
        orElse: () => throw Exception("Characteristic not found"),
      );

      await _dataCharacteristic!.setNotifyValue(true);
      _dataCharacteristic!.value.listen((value) {
        _processData(value);
      });

      setState(() {
        _connectionStatus = "Connected to ${widget.device.name}";
      });
    } catch (e) {
      setState(() {
        _connectionStatus = "Connection failed: ${e.toString()}";
      });
    }
  }

  void _processData(List<int> value) {
    try {
      String data = String.fromCharCodes(value);
      List<String> parts = data.split(',');
      
      if (parts.length >= 4) {
        setState(() {
          _steps = int.tryParse(parts[1]) ?? _steps;
          _cadence = double.tryParse(parts[2]) ?? _cadence;
          _lastAccel = double.tryParse(parts[3]) ?? _lastAccel;
          
          _accelHistory.removeAt(0);
          _accelHistory.add(_lastAccel);
          
          if (_lastAccel > 15.0) {
            _status = "SEIZURE ALERT!";
            _events.add({
              'time': DateTime.now(),
              'type': 'Seizure',
              'severity': _lastAccel,
            });
            _triggerAlert("Seizure detected!");
          } else if (_lastAccel > 3.0) {
            _status = "Fall detected";
            _events.add({
              'time': DateTime.now(),
              'type': 'Fall',
              'severity': _lastAccel,
            });
            _triggerAlert("Fall detected!");
          } else {
            _status = "Normal";
          }
        });
      }
    } catch (e) {
      debugPrint("Error processing data: $e");
    }
  }

  void _triggerAlert(String message) async {
    if (await Vibration.hasVibrator() ?? false) {
      Vibration.vibrate(duration: 1000);
    }
    
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text("Alert!"),
        content: Text(message),
        actions: [
          TextButton(
            child: const Text("OK"),
            onPressed: () => Navigator.of(ctx).pop(),
          ),
          if (_emergencyContact.isNotEmpty)
            TextButton(
              child: const Text("Call Emergency"),
              onPressed: () => _callEmergencyContact(),
            ),
        ],
      ),
    );
  }

  Future<void> _callEmergencyContact() async {
    final url = Uri.parse('tel:$_emergencyContact');
    if (await canLaunchUrl(url)) {
      await launchUrl(url);
    } else {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text("Could not launch phone")),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(widget.device.name),
        actions: [
          IconButton(
            icon: const Icon(Icons.history),
            onPressed: () => Navigator.push(
              context,
              MaterialPageRoute(
                builder: (context) => EventHistoryScreen(events: _events),
              ),
            ),
          ),
        ],
      ),
      body: SingleChildScrollView(
        child: Column(
          children: [
            Padding(
              padding: const EdgeInsets.all(16.0),
              child: Text(_connectionStatus),
            ),
            Card(
              margin: const EdgeInsets.all(16.0),
              child: Padding(
                padding: const EdgeInsets.all(16.0),
                child: Column(
                  children: [
                    const Text("Current Status", 
                      style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                    Text(_status, 
                      style: TextStyle(
                        fontSize: 24,
                        color: _status.contains("ALERT") ? Colors.red : Colors.green,
                      ),
                    ),
                  ],
                ),
              ),
            ),
            Card(
              margin: const EdgeInsets.all(16.0),
              child: Padding(
                padding: const EdgeInsets.all(16.0),
                child: Column(
                  children: [
                    const Text("Gait Metrics", 
                      style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                      children: [
                        Column(
                          children: [
                            const Text("Steps"),
                            Text("$_steps", style: const TextStyle(fontSize: 24)),
                          ],
                        ),
                        Column(
                          children: [
                            const Text("Cadence"),
                            Text("${_cadence.toStringAsFixed(0)}/min", 
                              style: const TextStyle(fontSize: 24)),
                          ],
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
            Card(
              margin: const EdgeInsets.all(16.0),
              child: Padding(
                padding: const EdgeInsets.all(16.0),
                child: Column(
                  children: [
                    const Text("Acceleration History", 
                      style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                    SizedBox(
                      height: 200,
                      child: SfCartesianChart(
                        primaryXAxis: NumericAxis(isVisible: false),
                        series: <LineSeries<double, int>>[
                          LineSeries<double, int>(
                            dataSource: _accelHistory,
                            xValueMapper: (_, index) => index,
                            yValueMapper: (y, _) => y,
                            name: 'Acceleration (g)',
                          )
                        ],
                      ),
                    ),
                    Text("Current: ${_lastAccel.toStringAsFixed(2)}g"),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  final _contactController = TextEditingController();
  final _notesController = TextEditingController();

  @override
  void initState() {
    super.initState();
    _loadSettings();
  }

  Future<void> _loadSettings() async {
    final prefs = await SharedPreferences.getInstance();
    setState(() {
      _contactController.text = prefs.getString('emergencyContact') ?? "";
      _notesController.text = prefs.getString('medicalNotes') ?? "";
    });
  }

  Future<void> _saveSettings() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('emergencyContact', _contactController.text);
    await prefs.setString('medicalNotes', _notesController.text);
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text("Settings saved")),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text("Settings"),
      ),
      body: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          children: [
            TextField(
              controller: _contactController,
              decoration: const InputDecoration(
                labelText: "Emergency Contact Number",
                hintText: "Enter phone number with country code",
              ),
              keyboardType: TextInputType.phone,
            ),
            const SizedBox(height: 20),
            TextField(
              controller: _notesController,
              decoration: const InputDecoration(
                labelText: "Medical Notes",
                hintText: "Blood type, allergies, etc.",
              ),
              maxLines: 3,
            ),
            const SizedBox(height: 20),
            ElevatedButton(
              onPressed: _saveSettings,
              child: const Text("Save Settings"),
            ),
          ],
        ),
      ),
    );
  }
}

class EventHistoryScreen extends StatelessWidget {
  final List<Map<String, dynamic>> events;

  const EventHistoryScreen({super.key, required this.events});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text("Event History"),
      ),
      body: ListView.builder(
        itemCount: events.length,
        itemBuilder: (context, index) {
          final event = events[index];
          return ListTile(
            title: Text(event['type']),
            subtitle: Text(event['time'].toString()),
            trailing: Text("${event['severity'].toStringAsFixed(1)}g"),
          );
        },
      ),
    );
  }
}