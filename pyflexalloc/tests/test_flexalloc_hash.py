from flexalloc import hash


def test_hash_insert_lookup():
    htbl = hash.Hashtable(10)

    assert htbl.len == 0, "expected no entries to start"
    assert htbl.tbl_size == 10, "expected htbl to be sized for 10 entries"

    assert htbl.lookup("one") is None
    htbl.insert("one", 1)
    entry = htbl.lookup("one")
    assert entry.val == 1

    assert htbl.lookup("two") is None
    htbl.insert("two", 2)
    entry = htbl.lookup("two")
    assert entry.val == 2

    assert htbl.len == 2, "expected htbl to have 2 entries"

    htbl.remove("one")
    assert htbl.lookup("one") is None
