/*
    FitLife Backend Server
    -----------------------
    A simple C++ backend for the FitLife fitness app.
    Uses only standard C++ and POSIX sockets — no external libraries.

    Concepts used: structs, functions, loops, conditionals, file handling,
    arrays, string parsing (same level as Quiz Game / EMS projects).

    Compile (Linux / Mac):
        g++ server.cpp -o server
        ./server

    Runs on: http://localhost:8080
*/

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace std;

const int PORT = 8080;
const string DATA_FOLDER = "data/";

// -----------------------------------------
// STRUCT: represents one exercise
// -----------------------------------------
struct Exercise {
    string name;
    string muscle;
    int sets;
    string reps;
    int restSeconds;
};

// -----------------------------------------
// STRUCT: represents one meal
// -----------------------------------------
struct Meal {
    string type;
    string name;
    int calories;
};

// -----------------------------------------
// EXERCISE DATABASE (same data as the website)
// -----------------------------------------
Exercise beginnerExercises[4] = {
    {"Push-ups", "Chest, Triceps", 3, "8-12", 60},
    {"Bodyweight Squats", "Legs, Glutes", 3, "12-15", 60},
    {"Plank", "Core", 3, "30-45 sec", 45},
    {"Lunges", "Legs, Glutes", 3, "10 each leg", 60}
};

Exercise intermediateExercises[3] = {
    {"Diamond Push-ups", "Triceps, Chest", 4, "10-15", 60},
    {"Bulgarian Split Squats", "Legs, Glutes", 4, "12 each leg", 75},
    {"Mountain Climbers", "Core, Cardio", 4, "20 each leg", 45}
};

Exercise advancedExercises[3] = {
    {"Pistol Squats", "Legs, Balance", 4, "8 each leg", 90},
    {"Burpees", "Full Body", 5, "15-20", 60},
    {"Handstand Push-ups", "Shoulders, Triceps", 4, "6-10", 120}
};

// -----------------------------------------
// MEAL PLAN DATABASE
// -----------------------------------------
Meal weightLossMeals[5] = {
    {"Breakfast", "Protein Oatmeal Bowl", 400},
    {"Lunch", "Grilled Chicken Salad", 500},
    {"Snack", "Greek Yogurt & Fruit", 200},
    {"Dinner", "Baked Fish with Vegetables", 550},
    {"Snack", "Protein Shake", 150}
};

Meal muscleGainMeals[5] = {
    {"Breakfast", "High-Protein Breakfast", 650},
    {"Lunch", "Chicken & Rice Bowl", 750},
    {"Snack", "Peanut Butter Sandwich", 400},
    {"Dinner", "Steak & Potatoes", 800},
    {"Snack", "Post-Workout Shake", 200}
};

Meal maintenanceMeals[5] = {
    {"Breakfast", "Balanced Breakfast", 500},
    {"Lunch", "Turkey Wrap", 600},
    {"Snack", "Mixed Nuts & Fruit", 250},
    {"Dinner", "Grilled Chicken with Quinoa", 650},
    {"Snack", "Cottage Cheese Bowl", 200}
};

// -----------------------------------------
// FUNCTION: extract query/body value by key
// (very simple parser — works for key=value&key2=value2 format)
// -----------------------------------------
string getParam(const string& data, const string& key) {
    size_t pos = data.find(key + "=");
    if (pos == string::npos) return "";
    pos += key.length() + 1;
    size_t end = data.find("&", pos);
    if (end == string::npos) end = data.length();
    return data.substr(pos, end - pos);
}

// -----------------------------------------
// FUNCTION: build JSON for an exercise list
// -----------------------------------------
string exercisesToJSON(Exercise list[], int count) {
    stringstream json;
    json << "[";
    for (int i = 0; i < count; i++) {
        json << "{\"name\":\"" << list[i].name << "\","
             << "\"muscle\":\"" << list[i].muscle << "\","
             << "\"sets\":" << list[i].sets << ","
             << "\"reps\":\"" << list[i].reps << "\","
             << "\"restSeconds\":" << list[i].restSeconds << "}";
        if (i != count - 1) json << ",";
    }
    json << "]";
    return json.str();
}

// -----------------------------------------
// FUNCTION: build JSON for a meal plan
// -----------------------------------------
string mealsToJSON(Meal list[], int count, int totalCalories) {
    stringstream json;
    json << "{\"totalCalories\":" << totalCalories << ",\"meals\":[";
    for (int i = 0; i < count; i++) {
        json << "{\"type\":\"" << list[i].type << "\","
             << "\"name\":\"" << list[i].name << "\","
             << "\"calories\":" << list[i].calories << "}";
        if (i != count - 1) json << ",";
    }
    json << "]}";
    return json.str();
}

// -----------------------------------------
// FUNCTION: calculate BMI
// -----------------------------------------
double calculateBMI(double weightKg, double heightCm) {
    double heightM = heightCm / 100.0;
    return weightKg / (heightM * heightM);
}

string bmiCategory(double bmi) {
    if (bmi < 18.5) return "Underweight";
    if (bmi < 25) return "Normal";
    if (bmi < 30) return "Overweight";
    return "Obese";
}

// -----------------------------------------
// FUNCTION: calculate daily calorie target
// -----------------------------------------
int calculateCalories(double weight, double height, int age, string gender, string goal) {
    double bmr;
    if (gender == "male") {
        bmr = 10 * weight + 6.25 * height - 5 * age + 5;
    } else {
        bmr = 10 * weight + 6.25 * height - 5 * age - 161;
    }
    double tdee = bmr * 1.55; // moderate activity multiplier

    if (goal == "weight_loss") return (int)(tdee - 500);
    if (goal == "muscle_gain") return (int)(tdee + 300);
    return (int)tdee; // maintenance
}

// -----------------------------------------
// FUNCTION: save a line of data to a file (append)
// -----------------------------------------
void saveToFile(const string& filename, const string& line) {
    ofstream file(DATA_FOLDER + filename, ios::app);
    if (file.is_open()) {
        file << line << "\n";
        file.close();
    }
}

// -----------------------------------------
// FUNCTION: read all lines from a file as JSON array of strings
// -----------------------------------------
string readFileAsJSON(const string& filename) {
    ifstream file(DATA_FOLDER + filename);
    stringstream json;
    json << "[";
    string line;
    bool first = true;
    while (getline(file, line)) {
        if (!first) json << ",";
        json << "\"" << line << "\"";
        first = false;
    }
    json << "]";
    return json.str();
}

// -----------------------------------------
// FUNCTION: send an HTTP response with CORS headers
// -----------------------------------------
void sendResponse(int clientSocket, const string& body, const string& contentType = "application/json") {
    stringstream response;
    response << "HTTP/1.1 200 OK\r\n"
              << "Content-Type: " << contentType << "\r\n"
              << "Access-Control-Allow-Origin: *\r\n"
              << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
              << "Access-Control-Allow-Headers: Content-Type\r\n"
              << "Content-Length: " << body.length() << "\r\n"
              << "Connection: close\r\n\r\n"
              << body;
    string responseStr = response.str();
    send(clientSocket, responseStr.c_str(), responseStr.length(), 0);
}

// -----------------------------------------
// FUNCTION: handle one incoming HTTP request
// -----------------------------------------
void handleRequest(int clientSocket, const string& request) {
    // Parse the request line: METHOD /path HTTP/1.1
    stringstream ss(request);
    string method, path, httpVersion;
    ss >> method >> path >> httpVersion;

    // Split path and query string
    string route = path;
    string query = "";
    size_t qPos = path.find("?");
    if (qPos != string::npos) {
        route = path.substr(0, qPos);
        query = path.substr(qPos + 1);
    }

    // Extract body (after the blank line) for POST requests
    string body = "";
    size_t bodyPos = request.find("\r\n\r\n");
    if (bodyPos != string::npos) {
        body = request.substr(bodyPos + 4);
    }

    // Handle CORS preflight
    if (method == "OPTIONS") {
        sendResponse(clientSocket, "");
        return;
    }

    // ---------- ROUTES ----------
    if (route == "/api/workouts" && method == "GET") {
        string level = getParam(query, "level");
        if (level == "beginner")
            sendResponse(clientSocket, exercisesToJSON(beginnerExercises, 4));
        else if (level == "intermediate")
            sendResponse(clientSocket, exercisesToJSON(intermediateExercises, 3));
        else if (level == "advanced")
            sendResponse(clientSocket, exercisesToJSON(advancedExercises, 3));
        else
            sendResponse(clientSocket, "{\"error\":\"invalid level\"}");
    }
    else if (route == "/api/diet" && method == "GET") {
        string goal = getParam(query, "goal");
        if (goal == "weight_loss")
            sendResponse(clientSocket, mealsToJSON(weightLossMeals, 5, 1800));
        else if (goal == "muscle_gain")
            sendResponse(clientSocket, mealsToJSON(muscleGainMeals, 5, 2800));
        else if (goal == "maintenance")
            sendResponse(clientSocket, mealsToJSON(maintenanceMeals, 5, 2200));
        else
            sendResponse(clientSocket, "{\"error\":\"invalid goal\"}");
    }
    else if (route == "/api/bmi" && method == "POST") {
        double weight = stod(getParam(body, "weight"));
        double height = stod(getParam(body, "height"));
        double bmi = calculateBMI(weight, height);
        stringstream json;
        json << "{\"bmi\":" << bmi << ",\"category\":\"" << bmiCategory(bmi) << "\"}";
        sendResponse(clientSocket, json.str());
    }
    else if (route == "/api/calories" && method == "POST") {
        double weight = stod(getParam(body, "weight"));
        double height = stod(getParam(body, "height"));
        int age = stoi(getParam(body, "age"));
        string gender = getParam(body, "gender");
        string goal = getParam(body, "goal");
        int calories = calculateCalories(weight, height, age, gender, goal);
        stringstream json;
        json << "{\"calories\":" << calories << "}";
        sendResponse(clientSocket, json.str());
    }
    else if (route == "/api/water" && method == "POST") {
        string date = getParam(body, "date");
        string amount = getParam(body, "amount");
        saveToFile("water.txt", date + ":" + amount);
        sendResponse(clientSocket, "{\"status\":\"saved\"}");
    }
    else if (route == "/api/water" && method == "GET") {
        sendResponse(clientSocket, readFileAsJSON("water.txt"));
    }
    else if (route == "/api/weight" && method == "POST") {
        string date = getParam(body, "date");
        string weight = getParam(body, "weight");
        saveToFile("weight.txt", date + ":" + weight);
        sendResponse(clientSocket, "{\"status\":\"saved\"}");
    }
    else if (route == "/api/weight" && method == "GET") {
        sendResponse(clientSocket, readFileAsJSON("weight.txt"));
    }
    else if (route == "/api/steps" && method == "POST") {
        string date = getParam(body, "date");
        string steps = getParam(body, "steps");
        saveToFile("steps.txt", date + ":" + steps);
        sendResponse(clientSocket, "{\"status\":\"saved\"}");
    }
    else if (route == "/api/steps" && method == "GET") {
        sendResponse(clientSocket, readFileAsJSON("steps.txt"));
    }
    else {
        sendResponse(clientSocket, "{\"error\":\"route not found\"}");
    }
}

// -----------------------------------------
// MAIN: sets up the socket server and listens for connections
// -----------------------------------------
int main() {
    // Create the data folder if needed (simple system call)
    system(("mkdir -p " + DATA_FOLDER).c_str());

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == 0) {
        cout << "Error: could not create socket\n";
        return 1;
    }

    // Allow reusing the port immediately after restart
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr*)&address, sizeof(address)) < 0) {
        cout << "Error: bind failed\n";
        return 1;
    }

    if (listen(serverSocket, 10) < 0) {
        cout << "Error: listen failed\n";
        return 1;
    }

    cout << "==========================================\n";
    cout << "   FitLife Backend Server\n";
    cout << "==========================================\n";
    cout << "Server running at http://localhost:" << PORT << "\n";
    cout << "Available endpoints:\n";
    cout << "  GET  /api/workouts?level=beginner\n";
    cout << "  GET  /api/diet?goal=weight_loss\n";
    cout << "  POST /api/bmi        (weight, height)\n";
    cout << "  POST /api/calories   (weight, height, age, gender, goal)\n";
    cout << "  GET  /api/water   | POST /api/water   (date, amount)\n";
    cout << "  GET  /api/weight  | POST /api/weight  (date, weight)\n";
    cout << "  GET  /api/steps   | POST /api/steps   (date, steps)\n";
    cout << "==========================================\n";

    // Main loop: accept and handle one connection at a time
    while (true) {
        sockaddr_in clientAddress;
        socklen_t clientLen = sizeof(clientAddress);
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientLen);

        if (clientSocket < 0) continue;

        char buffer[4096] = {0};
        read(clientSocket, buffer, sizeof(buffer) - 1);
        string request(buffer);

        if (!request.empty()) {
            handleRequest(clientSocket, request);
        }

        close(clientSocket);
    }

    close(serverSocket);
    return 0;
}
