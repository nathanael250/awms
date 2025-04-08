"""Reset and recreate valve_operation table

Revision ID: b1edebb3a9eb
Revises: 
Create Date: 2025-04-07 13:00:00.000000

"""
from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import mysql

# revision identifiers, used by Alembic.
revision = 'b1edebb3a9eb'
down_revision = None
branch_labels = None
depends_on = None


def upgrade():
    # Create temporary table
    op.execute("""
        CREATE TABLE valve_operation_new (
            id INT AUTO_INCREMENT PRIMARY KEY,
            user_id INT NOT NULL,
            counter_id VARCHAR(50) NOT NULL,
            action VARCHAR(20) NOT NULL,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            duration INT,
            FOREIGN KEY (user_id) REFERENCES user(id),
            FOREIGN KEY (counter_id) REFERENCES counter(counter_id)
        )
    """)
    
    # Copy data from old table to new table
    op.execute("""
        INSERT INTO valve_operation_new (user_id, counter_id, action, timestamp, duration)
        SELECT vo.user_id, u.counter_id, vo.operation, vo.timestamp, vo.duration
        FROM valve_operation vo
        JOIN user u ON vo.user_id = u.id
        WHERE u.counter_id IS NOT NULL
    """)
    
    # Drop old table
    op.drop_table('valve_operation')
    
    # Rename new table to valve_operation
    op.execute("RENAME TABLE valve_operation_new TO valve_operation")


def downgrade():
    # Create old table structure
    op.execute("""
        CREATE TABLE valve_operation_old (
            id INT AUTO_INCREMENT PRIMARY KEY,
            user_id INT NOT NULL,
            operation VARCHAR(20) NOT NULL,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            duration INT,
            FOREIGN KEY (user_id) REFERENCES user(id)
        )
    """)
    
    # Copy data back
    op.execute("""
        INSERT INTO valve_operation_old (user_id, operation, timestamp, duration)
        SELECT user_id, action, timestamp, duration
        FROM valve_operation
    """)
    
    # Drop new table
    op.drop_table('valve_operation')
    
    # Rename old table back to valve_operation
    op.execute("RENAME TABLE valve_operation_old TO valve_operation")
