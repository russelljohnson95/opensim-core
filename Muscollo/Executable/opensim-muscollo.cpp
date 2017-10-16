#include <iostream>
#include <Muscollo/InverseMuscleSolver/GlobalStaticOptimization.h>
#include <Muscollo/InverseMuscleSolver/INDYGO.h>
#include <Muscollo/MucoTool.h>
#include <Muscollo/MucoProblem.h>

#include <OpenSim/Common/Object.h>
#include <OpenSim/Simulation/osimSimulation.h>

using namespace OpenSim;

static const char helpMessage[] =
R"(OpenSim Muscollo. Use this command to run a setup file for the following:
  - Global Static Optimization,
  - INDYGO: Inverse, Dynamic, Global Optimization (tracking),
  - MucoTool: flexible optimal control framework (.omuco file).

Usage:
  opensim-muscollo -h | --help
  opensim-muscollo run-tool <setup-file>
  opensim-muscollo print-xml <tool>

    <tool> can be "GlobalStaticOptimization", "INDYGO", or "MucoTool"
)";

int main(int argc, char* argv[]) {

    try {

        std::string arg1(argv[1]);
        if (argc == 1 || arg1 == "-h" || arg1 == "--help") {
            std::cout << helpMessage << std::endl;
            return EXIT_SUCCESS;

        } else if (arg1 == "print-xml") {
            OPENSIM_THROW_IF(argc != 3, Exception,
                    "Incorrect number of arguments.");

            std::string className(argv[2]);
            if (className != "GlobalStaticOptimization" &&
                    className != "INDYGO" && className != "MucoTool") {
                throw Exception("Unexpected argument: " + className);
            }
            const auto* obj = Object::getDefaultInstanceOfType(argv[2]);
            if (!obj) {
                throw Exception("Cannot create an instance of " + className +
                        ".");
            }
            std::string fileName = "default_" + className;
            if (className == "MucoTool") fileName += ".omuco";
            else fileName += ".xml";
            std::cout << "Printing '" << fileName << "'." << std::endl;
            Object::setSerializeAllDefaults(true);
            obj->print(fileName);
            Object::setSerializeAllDefaults(false);

        } else if (arg1 == "run-tool") {
            OPENSIM_THROW_IF(argc != 3, Exception,
                    "Incorrect number of arguments.");

            const std::string setupFile(argv[2]);
            auto obj = std::unique_ptr<Object>(
                    Object::makeObjectFromFile(setupFile));

            OPENSIM_THROW_IF(obj == nullptr, Exception,
                    "A problem occurred when trying to load file '" + setupFile
                            + "'.");

            if (const auto* gso =
                    dynamic_cast<const GlobalStaticOptimization*>(obj.get())) {
                auto solution = gso->solve();
            }
            else if (const auto* mrs =
                    dynamic_cast<const INDYGO*>(obj.get())) {
                auto solution = mrs->solve();
            }
            else if (const auto* muco
                    = dynamic_cast<const MucoTool*>(obj.get())) {
                auto solution = muco->solve();
            }
            else {
                throw Exception("The provided file '" + setupFile +
                        "' yields a '" + obj->getConcreteClassName() +
                        "' but only GlobalStaticOptimization, INDYGO, and "
                        "MucoTool are acceptable.");
            }
        } else {
            std::cout << "Unrecognized arguments. See usage with -h or --help"
                    "." << std::endl;
        }

    } catch (const std::exception& exc) {
        std::cout << exc.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
