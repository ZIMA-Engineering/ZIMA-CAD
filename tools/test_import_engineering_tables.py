import runpy
import tempfile
import unittest
from pathlib import Path


IMPORTER = runpy.run_path(str(Path(__file__).with_name("import-engineering-tables.py")))


class EngineeringTableImportTest(unittest.TestCase):
    def test_imports_reference_content_and_migrates_metadata(self):
        temporary_root = Path(__file__).resolve().parents[1] / "build"
        temporary_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temporary_root) as temporary:
            root = Path(temporary)
            source = root / "04-MECHANICAL_TABLES"
            destination = root / "config" / "lib" / "03-ENGINEERING-TABLES"
            child = source / "01-A very long visible engineering directory"
            index = child / "0000-index"
            index.mkdir(parents=True)
            common = source / "0000-index"
            common.mkdir(parents=True)
            (common / "common.css").write_text("body{background:url(icon.png)}", encoding="utf-8")
            (common / "icon.png").write_bytes(b"icon")
            (index / "metadata.ini").write_text(
                "[Directory]\nLabel\\cs=Viditelný název\nVersion=2\n\n"
                "[Parts]\nrow\\value=42\n",
                encoding="utf-8",
            )
            (index / "01.png").write_bytes(b"png")
            (child / "table.pdf").write_bytes(b"pdf")
            (child / "legacy.prt.7").write_bytes(b"cad")
            (index / "index_cs.html").write_text('<link href="../../0000-index/common.css">legacy', encoding="utf-8")
            (index / "style.css").write_text("body{}", encoding="utf-8")
            (index / "table.js").write_text("void 0", encoding="utf-8")
            destination.mkdir(parents=True)
            (destination / "README.md").write_text("reserved", encoding="utf-8")

            report = IMPORTER["import_tree"](source, destination)
            mapping, _, _ = IMPORTER["plan_import"](source)
            compact = destination / mapping[child]
            metadata = (compact / "0000-index" / "metadata.ini").read_text(encoding="utf-8")
            self.assertEqual(report["files"], 9)
            self.assertEqual(report["verifiedFiles"], 9)
            self.assertEqual(report["localizedHtml"], 1)
            self.assertEqual(report["verifiedTransformedFiles"], 1)
            self.assertEqual(report["verifiedMetadata"], 2)
            self.assertEqual(len(report["contentTreeSha256"]), 64)
            self.assertIn("Version=3", metadata)
            self.assertIn("Kind=directory", metadata)
            self.assertIn("Label\\cs=Viditelný název", metadata)
            self.assertIn("Label\\en=A very long visible engineering directory", metadata)
            self.assertIn("row\\value=42", metadata)
            self.assertTrue((compact / "table.pdf").is_file())
            self.assertTrue((compact / "0000-index" / "01.png").is_file())
            self.assertFalse(any(destination.rglob("*.prt*")))
            self.assertTrue((compact / "0000-index" / "index_cs.html").is_file())
            self.assertIn('href="assets/common.css"',(compact / "0000-index" / "index_cs.html").read_text(encoding="utf-8"))
            self.assertTrue((compact / "0000-index" / "assets" / "common.css").is_file())
            self.assertTrue((compact / "0000-index" / "assets" / "icon.png").is_file())
            self.assertTrue((compact / "0000-index" / "style.css").is_file())
            self.assertTrue((compact / "0000-index" / "table.js").is_file())
            self.assertNotIn("d001", compact.parts)
            self.assertLessEqual(report["maximumPackageMemberLength"], 180)
            verified = IMPORTER["verify_import"](source, destination)
            self.assertEqual(verified["contentTreeSha256"], report["contentTreeSha256"])


if __name__ == "__main__":
    unittest.main()
