// SDACS Flutter component: Platform-aware helper for exporting capture artifacts from the Flutter UI.
//
// Role in system: presents or transports Raspberry Pi backend state without
// duplicating firmware signal-processing logic in the client.

class FileExportService {
  const FileExportService();

  Future<void> downloadCsv() async {
    // TODO: Request CSV export from the backend.
    await Future<void>.delayed(const Duration(milliseconds: 300));
  }

  Future<void> downloadDatabase() async {
    // TODO: Request database export from the backend.
    await Future<void>.delayed(const Duration(milliseconds: 300));
  }
}
