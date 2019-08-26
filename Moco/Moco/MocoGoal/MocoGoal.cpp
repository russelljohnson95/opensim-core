/* -------------------------------------------------------------------------- *
 * OpenSim Moco: MocoGoal.cpp                                                 *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2017 Stanford University and the Authors                     *
 *                                                                            *
 * Author(s): Christopher Dembia                                              *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0          *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */
#include "MocoGoal.h"

using namespace OpenSim;

MocoGoal::MocoGoal() {
    constructProperties();
    if (getName().empty()) setName("goal");
}

MocoGoal::MocoGoal(std::string name) {
    setName(std::move(name));
    constructProperties();
}

MocoGoal::MocoGoal(std::string name, double weight)
        : MocoGoal(std::move(name)) {
    set_weight(weight);
}


void MocoGoal::printDescription(std::ostream& stream) const {
    const auto mode = getModeAsString();
    stream << getName() << ". " << getConcreteClassName() <<
            " enabled: " << get_enabled() << " mode: " << mode;
    if (mode == "cost") {
        stream << " weight: " << get_weight() << std::endl;
    }
    printDescriptionImpl(stream);
}

void MocoGoal::constructProperties() {
    constructProperty_enabled(true);
    constructProperty_weight(1);
    constructProperty_mode();
    constructProperty_MocoConstraintInfo(MocoConstraintInfo());
}
