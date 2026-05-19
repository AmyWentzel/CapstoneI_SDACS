import 'package:flutter/material.dart';

class SdacsAppBar extends StatelessWidget implements PreferredSizeWidget {
  const SdacsAppBar({super.key, required this.title, this.actions});

  final String title;
  final List<Widget>? actions;

  @override
  Size get preferredSize => const Size.fromHeight(kToolbarHeight);

  @override
  Widget build(BuildContext context) {
    return AppBar(title: Text(title), actions: actions);
  }
}
