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
  {"opt1", "A", "A-NOR", false},
  {"opt2", "B", "B-NOR", false},
  {"opt3", "C", "C-NOR", false},
  {"opt4", "D", "D-FRO", false},
  {"opt5", "E", "E-GBR", false},
  {"opt6", "G", "G-GBR", false},
  {"opt7", "H", "H-SWE", false},
  {"opt8", "I", "I-SWE", false},
  {"opt9", "J", "J-SWE", false},
  {"opt10", "K", "K-GBR", false},
  {"opt11", "L", "L-NOR", false},
  {"opt12", "M", "M-NOR", false},
  {"opt13", "N", "N-NOR", false},
  {"opt14", "O", "O-GBR", false},
  {"opt15", "P", "P-NLD", false},
  {"opt16", "Q", "Q-IRL", false},
  {"opt17", "R", "R-ISL", false},
  {"opt18", "S", "S-DEU", false},
  {"opt19", "T", "T-BEL", false},
  {"opt20", "F", "F-EST", false},
  {"opt21", "V", "V-BEL", false},
  {"opt22", "W", "W-IRL", false},
  {"opt23", "X", "X-ISL", false},
  {"opt24", "U", "U-NOR", false}
};
