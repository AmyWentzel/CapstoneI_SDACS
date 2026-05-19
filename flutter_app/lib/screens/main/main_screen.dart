import 'package:flutter/material.dart';

import '../../app/app_routes.dart';
import '../../models/calibration_result.dart';
import '../../models/node_telemetry.dart';
import '../../services/sdacs_api_service.dart';
import '../../widgets/sdacs_app_bar.dart';
import 'widgets/latest_calibration_card.dart';
import 'widgets/main_action_button.dart';
import 'widgets/node_status_card.dart';

class MainScreen extends StatelessWidget {
  const MainScreen({super.key});

  Future<void> _startTest(BuildContext context) async {
    final messenger = ScaffoldMessenger.of(context);
    await const SdacsApiService().startTestCapture();
    messenger.showSnackBar(
      const SnackBar(content: Text('Placeholder test capture started.')),
    );
  }

  @override
  Widget build(BuildContext context) {
    final nodes = NodeTelemetry.mockList();

    return Scaffold(
      appBar: const SdacsAppBar(title: 'SDACS Dashboard'),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Text(
            'Control Panel',
            style: Theme.of(
              context,
            ).textTheme.headlineSmall?.copyWith(fontWeight: FontWeight.w800),
          ),
          const SizedBox(height: 16),
          GridView.count(
            crossAxisCount: MediaQuery.sizeOf(context).width >= 720 ? 4 : 2,
            crossAxisSpacing: 12,
            mainAxisSpacing: 12,
            childAspectRatio: 2.7,
            shrinkWrap: true,
            physics: const NeverScrollableScrollPhysics(),
            children: [
              MainActionButton(
                icon: Icons.settings_outlined,
                label: 'Setup',
                onPressed: () => Navigator.pushNamed(context, AppRoutes.setup),
              ),
              MainActionButton(
                icon: Icons.tune,
                label: 'Run Calibration',
                // TODO: Disable this until setup completion is tracked.
                onPressed: () =>
                    Navigator.pushNamed(context, AppRoutes.calibration),
              ),
              MainActionButton(
                icon: Icons.play_arrow,
                label: 'Test',
                onPressed: () => _startTest(context),
              ),
              MainActionButton(
                icon: Icons.insights,
                label: 'View Results',
                onPressed: () => Navigator.pushNamed(context, AppRoutes.graphs),
              ),
            ],
          ),
          const SizedBox(height: 24),
          LatestCalibrationCard(result: CalibrationResult.placeholder()),
          const SizedBox(height: 24),
          Text('Nodes', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 12),
          GridView.builder(
            itemCount: nodes.length,
            shrinkWrap: true,
            physics: const NeverScrollableScrollPhysics(),
            gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
              crossAxisCount: MediaQuery.sizeOf(context).width >= 900 ? 4 : 2,
              crossAxisSpacing: 12,
              mainAxisSpacing: 12,
              childAspectRatio: 1.45,
            ),
            itemBuilder: (context, index) {
              return NodeStatusCard(telemetry: nodes[index]);
            },
          ),
        ],
      ),
    );
  }
}
