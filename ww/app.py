from flask import Flask, request, jsonify, render_template

app = Flask(__name__)

water_usage = {}

# Admin Routes
@app.route('/admin')
def admin_dashboard():
    return render_template('adminDashboard.html')

@app.route('/users')
def users():
    return render_template('users.html')

@app.route('/water-usage')
def water_usage_page():
    return render_template('water-usage.html')

@app.route('/billing')
def billing():
    return render_template('billing.html')

@app.route('/settings')
def admin_settings():
    return render_template('settings.html')

# User Routes
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/user/dashboard')
def user_dashboard():
    return render_template('User_dashboard.html')

@app.route('/user/consumption')
def consumption():
    return render_template('consumption.html')

@app.route('/user/bills')
def bills():
    return render_template('bills.html')

@app.route('/user/settings')
def user_settings():
    return render_template('User-settings.html')

@app.route('/user/support')
def support():
    return render_template('support.html')

@app.route('/user/report')
def user_report():
    return render_template('user-report.html')

# API Endpoints
@app.route('/api/receive-data', methods=['POST'])
def receive_data():
    data = request.json
    user_id = data.get('user_id')
    usage = data.get('usage')

    if user_id and usage:
        water_usage[user_id] = usage
        print(f"User {user_id} used {usage}L of water")
        return jsonify({"message": "Data received successfully"}), 200
    return jsonify({"error": "Invalid data"}), 400

@app.route('/api/water-usage', methods=['GET'])
def get_data():
    return jsonify(water_usage)

if __name__ == '__main__':
    app.run(debug=True, host="0.0.0.0", port=5000)
