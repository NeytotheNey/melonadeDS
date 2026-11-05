package me.magnum.melonds.common.retroachievements

import me.magnum.melonds.impl.FakeSharedPreferences
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Document
import org.w3c.dom.Element
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory

/**
 * The backup rules name the key's file in XML, where nothing else would notice them going out of step with the code.
 */
class WebApiKeyBackupRulesTest {

    private val moduleDir = listOf(File("."), File("app")).first { File(it, "src/main/res/xml").isDirectory }
    private val keyFile = "${AndroidRAWebApiKeyStore.PREFERENCES_FILE}.xml"

    private fun parse(path: String): Document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(File(moduleDir, path))

    private fun Element.children(): List<Element> {
        return (0 until childNodes.length).map { childNodes.item(it) }.filterIsInstance<Element>()
    }

    // An include anywhere would turn the rules into "only this", dropping everything else from the backups
    private fun assertOnlyExcludesTheKeyFile(section: Element) {
        val rule = section.children().single()
        assertEquals("exclude", rule.tagName)
        assertEquals("sharedpref", rule.getAttribute("domain"))
        assertEquals(keyFile, rule.getAttribute("path"))
    }

    @Test
    fun oldAndroidVersionsLeaveOutOnlyTheKeyFile() {
        val rules = parse("src/main/res/xml/backup_rules.xml").documentElement

        assertEquals("full-backup-content", rules.tagName)
        assertOnlyExcludesTheKeyFile(rules)
    }

    @Test
    fun newAndroidVersionsLeaveOutOnlyTheKeyFileFromBothCloudAndTransfer() {
        val rules = parse("src/main/res/xml/data_extraction_rules.xml").documentElement

        assertEquals("data-extraction-rules", rules.tagName)
        assertEquals(listOf("cloud-backup", "device-transfer"), rules.children().map { it.tagName })
        rules.children().forEach(::assertOnlyExcludesTheKeyFile)
    }

    @Test
    fun theManifestUsesBothRuleFiles() {
        val application = parse("src/main/AndroidManifest.xml").getElementsByTagName("application").item(0) as Element

        assertEquals("@xml/backup_rules", application.getAttribute("android:fullBackupContent"))
        assertEquals("@xml/data_extraction_rules", application.getAttribute("android:dataExtractionRules"))
    }

    @Test
    fun theStoreKeepsAndForgetsTheKey() {
        val preferences = FakeSharedPreferences()
        val store = AndroidRAWebApiKeyStore(preferences)

        assertNull(store.getApiKey())
        store.storeApiKey("key")
        assertEquals("key", store.getApiKey())
        assertTrue(preferences.all.isNotEmpty())

        store.clearApiKey()
        assertNull(store.getApiKey())
    }
}
