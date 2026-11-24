#pragma once

#include "../Command.h"

#include <string>

namespace plynth {

// Gives an item a generator with these values and the size worked out from them, for the parameter fields of
// the Properties panel and "plynth item params". The caller makes use and size with makeGeneratorUse
// (Generators.h) first, so the command never opens the library. Works on any item: an asset item becomes a
// generator item (its asset is cleared). Position, rotation, room and z stay; a wall item is put flush again for
// the new depth. Changes with the same non zero mergeKey and item merge into one undo step. Changes Items only.
// NotFound for an unknown item, otherwise as checkItemPlacement (Items.h).
class SetItemGeneratorCommand : public Command {
public:
    SetItemGeneratorCommand(Id itemId, GeneratorUse use, ItemSize size, int mergeKey = 0);

    std::string name() const override;
    Status apply(Project& project) override;
    void undo(Project& project) override;
    unsigned changes() const override;
    Id floorId() const override;
    bool mergeWith(const Command& next) override;

private:
    Id itemId_;
    GeneratorUse use_;
    ItemSize size_;
    int mergeKey_;
    Id floorId_;
    Item previous_;
};

}
