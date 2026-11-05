package me.magnum.melonds.database.migrations

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.File

/**
 * Room refuses to open a database whose tables differ from the schema it expects. [Migration8to9Test] checks this on a device, this catches the
 * same mistake without one.
 */
class Migration8to9SchemaTest {

    private val moduleDir = listOf(File("."), File("app")).first { File(it, "schemas").isDirectory }

    @Test
    fun theMigrationCreatesTheTableTheSchemaExpects() {
        val schema = JSONObject(File(moduleDir, "schemas/me.magnum.melonds.database.MelonDatabase/9.json").readText())
        val entities = schema.getJSONObject("database").getJSONArray("entities")
        val table = (0 until entities.length()).map { entities.getJSONObject(it) }.single { it.getString("tableName") == "ra_game_metadata" }
        val expectedSql = table.getString("createSql").replace("\${TABLE_NAME}", "ra_game_metadata")

        assertEquals(normalize(expectedSql), normalize(Migration8to9.CREATE_RA_GAME_METADATA))
    }

    private fun normalize(sql: String) = sql.replace(Regex("\\s+"), " ").replace("( ", "(").replace(" )", ")")
}
