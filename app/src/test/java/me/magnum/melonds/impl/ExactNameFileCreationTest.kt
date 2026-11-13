package me.magnum.melonds.impl

import org.junit.Assert.assertEquals
import org.junit.Test

class ExactNameFileCreationTest {

    private class FakeFile(val name: String?)

    private val deleted = mutableListOf<FakeFile>()

    private fun create(file: FakeFile?) = ExactNameFileCreation.create("Pokemon (NDSi Enhanced).sav", { file }, { it.name }, { deleted.add(it) })

    @Test
    fun fileWithTheRequestedNameIsKept() {
        val file = FakeFile("Pokemon (NDSi Enhanced).sav")

        assertEquals(ExactNameFileCreation.Result.Created(file), create(file))
        assertEquals(emptyList<FakeFile>(), deleted)
    }

    @Test
    fun renamedFileIsDeletedAndFails() {
        // What the Fold's provider did while it hid the folder but still saw the real save
        val file = FakeFile("Pokemon (NDSi Enhanced).sav (1)")

        assertEquals(ExactNameFileCreation.Result.Renamed("Pokemon (NDSi Enhanced).sav (1)"), create(file))
        assertEquals(listOf(file), deleted)
    }

    @Test
    fun fileWhoseNameCantBeReadIsDeletedAndFails() {
        val file = FakeFile(null)

        assertEquals(ExactNameFileCreation.Result.Renamed(null), create(file))
        assertEquals(listOf(file), deleted)
    }

    @Test
    fun noFileIsAFailure() {
        assertEquals(ExactNameFileCreation.Result.Failed, create(null))
        assertEquals(emptyList<FakeFile>(), deleted)
    }

    @Test
    fun saveStateNameIsCheckedTheSameWay() {
        val file = FakeFile("Pokemon.ml1 (2)")

        val result = ExactNameFileCreation.create("Pokemon.ml1", { file }, { it.name }, { deleted.add(it) })

        assertEquals(ExactNameFileCreation.Result.Renamed("Pokemon.ml1 (2)"), result)
        assertEquals(listOf(file), deleted)
    }
}
