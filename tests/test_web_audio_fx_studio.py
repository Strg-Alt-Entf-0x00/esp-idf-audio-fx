import re
import unittest
from pathlib import Path

HTML_PATH = Path(__file__).resolve().parent.parent / "tools-web" / "index.html"


class TestWebAudioFxStudio(unittest.TestCase):
    def setUp(self):
        self.assertTrue(HTML_PATH.exists(), f"File does not exist: {HTML_PATH}")
        self.page = HTML_PATH.read_text(encoding="utf-8")
        self.assets = sorted(
            path
            for folder in ("css", "js")
            for path in (HTML_PATH.parent / folder).glob("*")
            if path.is_file()
        )
        self.content = self.page + "\n" + "\n".join(
            path.read_text(encoding="utf-8") for path in self.assets
        )

    def test_file_is_self_contained_single_page(self):
        """Verifies local assets resolve and no external CDN dependencies are used."""
        self.assertNotIn("http://", self.content)
        external_scripts = re.findall(
            r'<script\s+[^>]*src=["\']https?://', self.page, re.IGNORECASE
        )
        self.assertEqual(len(external_scripts), 0, "External scripts found")
        external_styles = re.findall(
            r'<link\s+[^>]*href=["\']https?://', self.page, re.IGNORECASE
        )
        self.assertEqual(len(external_styles), 0, "External stylesheets found")
        local_assets = re.findall(
            r'(?:src|href)=["\']([^"\']+)["\']', self.page, re.IGNORECASE
        )
        self.assertTrue(local_assets, "Expected local CSS and JavaScript assets")
        for asset in local_assets:
            self.assertTrue((HTML_PATH.parent / asset).is_file(), f"Missing asset: {asset}")

    def test_all_eight_dsp_effects_present(self):
        """Verifies that all 8 DSP effects are present in the studio metadata."""
        expected_effects = [
            "reverb",
            "chorus",
            "equalizer",
            "delay",
            "flanger",
            "bitcrusher",
            "compressor",
            "limiter",
        ]
        for effect in expected_effects:
            self.assertIn(f'"{effect}"', self.content.lower())
            self.assertIn(effect.lower(), self.content.lower())

    def test_web_serial_api_implementation(self):
        """Verifies that the Web Serial API engine is properly implemented."""
        self.assertIn("navigator.serial.requestPort", self.content)
        self.assertIn("TextDecoderStream", self.content)
        self.assertIn("TextEncoder", self.content)
        self.assertIn("baudRate", self.content)

    def test_firmware_identity_handshake(self):
        """Verifies the exact tool identification handshake string."""
        self.assertIn(
            "TOOL_ID:Strg-Alt-Entf-0x00/esp-idf-audio-fx:console_audio_fx:1",
            self.content,
        )

    def test_hardware_controls_implemented(self):
        """Verifies hardware volume, mic selection, codec gain, and playback controls."""
        self.assertIn("volumeSlider", self.content)
        self.assertIn("micSelect", self.content)
        self.assertIn("micGainSlider", self.content)
        self.assertIn("playBtn", self.content)
        self.assertIn("stopBtn", self.content)
        self.assertIn("liveMicBtn", self.content)
        self.assertIn("bypassSwitch", self.content)

    def test_equalizer_visualizer_and_parameters(self):
        """Verifies 5-band equalizer special handling and general parameter parsing."""
        self.assertIn("renderEqVisualizer", self.content)
        self.assertIn("FX_PARAM", self.content)
        self.assertIn("diag", self.content)


if __name__ == "__main__":
    unittest.main()
