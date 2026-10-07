-- cave_data.lua
-- Keyed by [location_idx][quest]
-- location_idx from Navigation's screen_manifest (ROM 0x18480+room_id, bits 7-2)
-- quest: 1=Q1, 2=Q2, 0=wildcard

return {
    [1]  = { [0] = { label = "Eagle Dungeon",          dialog = nil } },
    [2]  = { [0] = { label = "Moon Dungeon",           dialog = nil } },
    [3]  = { [0] = { label = "Manji Dungeon",          dialog = nil } },
    [4]  = { [0] = { label = "Snake Dungeon",          dialog = nil } },
    [5]  = { [0] = { label = "Lizard Dungeon",         dialog = nil } },
    [6]  = { [0] = { label = "Dragon Dungeon",         dialog = nil } },
    [7]  = { [0] = { label = "Demon Dungeon",          dialog = nil } },
    [8]  = { [0] = { label = "Lion Dungeon",           dialog = nil } },
    [9]  = { [0] = { label = "Death Mountain",         dialog = nil } },

    -- Placeholder entries for loc IDs we haven't fully identified yet.
    -- These give us a fallback label so any cave the engine sees gets
    -- announced as something, even if we don't know the exact type yet.
    [10] = { [0] = { label = "Cave",                   dialog = nil } },
    [11] = { [0] = { label = "Cave",                   dialog = nil } },
    [12] = { [0] = { label = "Cave",                   dialog = nil } },
    [13] = { [0] = { label = "Cave",                   dialog = nil } },
    [14] = { [0] = { label = "Cave",                   dialog = nil } },
    [15] = { [0] = { label = "Cave",                   dialog = nil } },

    [16] = { [0] = { label = "Wood Sword Cave",        dialog = "It's dangerous to go alone. Take this." } },
    [17] = { [0] = { label = "Heart or Potion Cave",   dialog = "Take any one you want." } },
    [18] = { [0] = { label = "White Sword Cave",       dialog = "Master using it and you can have this." } },

    [19] = { [0] = { label = "Magical Sword Cave",    dialog = "Master using it and you can have this." } },
    [20] = { [0] = { label = "Any Road Cave",          dialog = "Take any road you want." } },
    [21] = { [0] = { label = "Hint Cave",              dialog = "Secret is in the tree at the dead-end." } },

    [22] = { [0] = { label = "Gambling Cave",          dialog = "Let's play money making game." } },
    [23] = { [0] = { label = "Door Repair Cave",       dialog = "Pay me for the door repair charge." } },
    [24] = { [0] = { label = "Letter Cave",            dialog = "Show this to the old woman." } },
    [25] = { [0] = { label = "Hint Cave",              dialog = "Meet the old man at the grave." } },
    [26] = { [0] = { label = "Potion Shop",            dialog = "Buy medicine before you go." } },
    [27] = { [0] = { label = "Information Cave",      dialog = "Pay me and I'll talk." } },
    [28] = { [0] = { label = "Information Cave",      dialog = "Pay me and I'll talk." } },
    [29] = { [0] = { label = "Shop",                   dialog = "Buy somethin' will ya." } },
    [30] = { [0] = { label = "Shop",                   dialog = "Buy somethin' will ya." } },
    [31] = { [0] = { label = "Shop",                   dialog = "Boy, this is really expensive!" } },
    [32] = { [0] = { label = "Shop",                   dialog = "Boy, this is really expensive!" } },
    [33] = { [0] = { label = "Secret Rupee Cave",      dialog = "It's a secret to everybody." } },
    [34] = { [0] = { label = "Secret Rupee Cave",      dialog = "It's a secret to everybody." } },
    [35] = { [0] = { label = "Secret Rupee Cave",      dialog = "It's a secret to everybody." } },

    [36] = { [0] = { label = "Cave",                   dialog = nil } },
    [37] = { [0] = { label = "Cave",                   dialog = nil } },

    [38] = { [0] = { label = "Take Any Road",          dialog = nil } },
}
