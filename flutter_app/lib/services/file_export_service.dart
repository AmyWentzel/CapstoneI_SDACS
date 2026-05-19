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
