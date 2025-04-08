from app import app, db
from flask_migrate import Migrate, upgrade
from sqlalchemy import text

# Initialize Flask-Migrate
migrate = Migrate(app, db)

def run_migrations():
    with app.app_context():
        # Add reset_token and reset_token_expiry columns to user table
        with db.engine.connect() as connection:
            connection.execute(text("""
                ALTER TABLE user 
                ADD COLUMN reset_token VARCHAR(100) UNIQUE,
                ADD COLUMN reset_token_expiry DATETIME;
            """))
            connection.commit()
        
if __name__ == "__main__":
    run_migrations() 