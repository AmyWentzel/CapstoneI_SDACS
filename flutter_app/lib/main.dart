// SDACS Flutter component: Flutter entry point for the SDACS application.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

import 'package:flutter/material.dart';

import 'app/sdacs_app.dart';

void main() {
  runApp(const SdacsApp());
}
