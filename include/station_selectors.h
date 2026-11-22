#pragma once

#include <string>
#include <vector>

struct InputOption {
    std::string id;
    std::string value;
    std::string label;
    bool isSelected;
};

std::vector<InputOption> stationSelectors = {
  {"opt1", "A", "Malin Head [A] 490kHz", false},
  {"opt2", "B", "Oostende [B] 490kHz", false},
  {"opt3", "C", "Torshavn [C] 518kHz", false},
  {"opt4", "D", "Torshavn Radio [D] 518kHz", false},
  {"opt5", "E", "Niton [E] 518kHz", false},
  {"opt6", "G", "Cullercoats [G] 518kHz", false},
  {"opt7", "H", "Bjuroklubb [H] 518kHz", false},
  {"opt8", "I", "Niton [I] 490kHz", false},
  {"opt9", "J", "Stockholm [J] 518kHz", false},
  {"opt10", "K", "Niton [K] 518kHz", false},
  {"opt11", "L", "Pinneberg [L] 490kHz", false},
  {"opt12", "M", "Jeloya [M] 518kHz", false},
  {"opt13", "N", "Orlandet [N] 518kHz", false},
  {"opt14", "O", "Portpatrick [O] 518kHz", false},
  {"opt15", "P", "Den Helder [P] 518kHz", false},
  {"opt16", "Q", "Malin Head [Q] 518kHz", false},
  {"opt17", "R", "Reykjavik [R] 518kHz", false},
  {"opt18", "S", "Pinneberg [S] 518kHz", false},
  {"opt19", "T", "Oostende [T] 518kHz", false},
  {"opt20", "F", "Tallin [F] 518kHz", false},
  {"opt21", "V", "Oostende [V] 518kHz", false},
  {"opt22", "W", "Valentia [W] 518kHz", false},
  {"opt23", "X", "Reykjavik [X] 518kHz", false},
  {"opt24", "U", "Cullercoats [U] 490kHz", false},
  {"opt25", "E", "Ouessant [E] 490kHz", false},
  {"opt26", "A", "CROSS Corsen [A] 518kHz", false},
  {"opt27", "S", "CROSS La Garde [S] 490kHz", false},
  {"opt28", "W", "CROSS La Garde [W] 518kHz", false}
};
