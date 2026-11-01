import builtins
import unittest
from unittest import mock

from tools.pm import launcher as launcher_mod


class LauncherDispatch(unittest.TestCase):
    def test_delegates_to_cli_when_import_ok(self):
        fake_cli = mock.Mock(return_value=0)
        with mock.patch("tools.pm.cli.main", fake_cli):
            self.assertEqual(launcher_mod.main(["version"]), 0)
        fake_cli.assert_called_once_with(["version"])

    def test_tools_import_failure_routes_to_repair(self):
        real_import = builtins.__import__

        def boom(name, *a, **k):
            if name == "tools.pm.cli":
                raise ModuleNotFoundError("No module named 'tools'", name="tools")
            return real_import(name, *a, **k)

        with mock.patch.object(builtins, "__import__", boom), \
             mock.patch.object(launcher_mod, "_repair", return_value=7) as repair:
            self.assertEqual(launcher_mod.main([]), 7)
        repair.assert_called_once_with("tools")

    def test_unrelated_import_error_reraises(self):
        real_import = builtins.__import__

        def boom(name, *a, **k):
            if name == "tools.pm.cli":
                raise ModuleNotFoundError("No module named 'numpy'", name="numpy")
            return real_import(name, *a, **k)

        with mock.patch.object(builtins, "__import__", boom):
            with self.assertRaises(ModuleNotFoundError):
                launcher_mod.main([])


class LauncherRepair(unittest.TestCase):
    def test_no_checkout_prints_manual_fix(self):
        with mock.patch("tools.pm.env.inspect_install") as inspect, \
             mock.patch("tools.pm.env.discover_sdk_root", return_value=None):
            inspect.return_value = mock.Mock(
                editable=False, editable_target=None, target_exists=None
            )
            self.assertEqual(launcher_mod._repair("tools"), 1)

    def test_non_interactive_does_not_run_pip(self):
        with mock.patch("tools.pm.env.inspect_install") as inspect, \
             mock.patch("tools.pm.env.discover_sdk_root", return_value="/live/sdk"), \
             mock.patch.object(launcher_mod, "_non_interactive", return_value=True), \
             mock.patch("tools.pm.env.run_pip_install") as run_pip:
            inspect.return_value = mock.Mock(
                editable=True, editable_target="/dead/sdk", target_exists=False
            )
            self.assertEqual(launcher_mod._repair("tools"), 1)
            run_pip.assert_not_called()

    def test_interactive_yes_runs_pip(self):
        with mock.patch("tools.pm.env.inspect_install") as inspect, \
             mock.patch("tools.pm.env.discover_sdk_root", return_value="/live/sdk"), \
             mock.patch.object(launcher_mod, "_non_interactive", return_value=False), \
             mock.patch.object(builtins, "input", return_value="y"), \
             mock.patch("tools.pm.env.run_pip_install", return_value=0) as run_pip:
            inspect.return_value = mock.Mock(
                editable=True, editable_target="/dead/sdk", target_exists=False
            )
            self.assertEqual(launcher_mod._repair("tools"), 0)
            run_pip.assert_called_once_with("/live/sdk", editable=True)

    def test_interactive_no_aborts(self):
        with mock.patch("tools.pm.env.inspect_install") as inspect, \
             mock.patch("tools.pm.env.discover_sdk_root", return_value="/live/sdk"), \
             mock.patch.object(launcher_mod, "_non_interactive", return_value=False), \
             mock.patch.object(builtins, "input", return_value="n"), \
             mock.patch("tools.pm.env.run_pip_install") as run_pip:
            inspect.return_value = mock.Mock(
                editable=True, editable_target="/dead/sdk", target_exists=False
            )
            self.assertEqual(launcher_mod._repair("tools"), 1)
            run_pip.assert_not_called()


if __name__ == "__main__":
    unittest.main()
